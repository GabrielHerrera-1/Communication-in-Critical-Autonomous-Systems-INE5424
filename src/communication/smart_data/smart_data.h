#ifndef SMART_DATA_H
#define SMART_DATA_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include "../../channel/vehicle_protocol.h"
#include "../../core/clock.h"
#include "../../core/periodic_thread.h"
#include "../../core/observers/concurrent_observer.h"
#include "../communicator.h"
#include "../iproducer.h"
#include "../message/message.h"
#include "smart_message.h"
#include "unit.h"
#include "binding_cache.h"
#include "smart_helpers.h"

struct Smart_Config {
    static constexpr uint64_t INTEREST_REFRESH_US     = 1'000'000; // Refresh reativo 1s de silencio -> reenvia
    static constexpr uint64_t BINDING_LIFETIME_MIN_US = 3'500'000; // binding expira apos 3.5s sem refresh
    static constexpr unsigned BINDING_LIFETIME_FACTOR = 4;
    static constexpr uint64_t REAPER_PERIOD_US        = 1'000'000;
    static constexpr unsigned VALUE_TTL_FACTOR        = 3;         // modo-valor: o ultimo valor e considerado fresco por ate N periodos sem atualizacao
};

// SmartData<Tipo>: abstracao Interesse/Resposta (etapa 5), observer do communicator
template <typename Type>
class SmartData : public Concurrent_Observer<Message, Vehicle_Protocol::Port> {
public:
    using Base    = Concurrent_Observer<Message, Vehicle_Protocol::Port>;
    using Value   = typename Type::Value;
    using Comm    = Communicator<Vehicle_Protocol>;
    using Address = Vehicle_Protocol::Address;
    using Port    = Vehicle_Protocol::Port;
    static constexpr Unit UNIT = Type::UNIT;

    enum Role { PRODUCER, CONSUMER };

    // produtor
    SmartData(Comm * comm, IProducer<Value> * producer)
        : _comm(comm), _role(PRODUCER), _producer(producer) {
        _cache = std::make_unique<Binding_Cache>(
            [this](Unit u) { respond(u); },
            Smart_Config::BINDING_LIFETIME_MIN_US,
            Smart_Config::BINDING_LIFETIME_FACTOR,
            Smart_Config::REAPER_PERIOD_US);
        _comm->attach(this, _comm->broadcast_condition()); // observa o Communicator
    }

    // consumidor
    SmartData(Comm * comm, uint64_t period_us, bool auto_refresh = true, bool value_mode = false)
        : _comm(comm), _role(CONSUMER), _period_us(period_us ? period_us : 1),
          _value_mode(value_mode) {
        _comm->attach(this, _comm->broadcast_condition());
        _last_response_ns.store(Clock::now_ns(), std::memory_order_relaxed);
        send_interest(false);
        if (auto_refresh) {
            // refresh reativo
            _refresh = std::make_unique<Periodic_Thread>(
                Smart_Config::INTEREST_REFRESH_US, [this] { reactive_tick(); });
        } else {
            for (int i = 0; i < 2; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                send_interest(false);
            }
        }
    }

    ~SmartData() override {
        _comm->detach(this, _comm->broadcast_condition()); // para de receber primeiro
        if (_role == CONSUMER) {
            if (_refresh) _refresh->stop();
            if (!_unsubscribed) send_interest(true);
            _refresh.reset();
        }
        _cache.reset();
        drain_and_free(*this); // libera mensagens que ficaram na fila
    }

    void update(Port c, Message * m) override {
        const SmartHeader * h = header_of(m);
        if (!h || h->unit != UNIT) { delete m; return; }

        if (_role == PRODUCER && h->kind == SmartHeader::INTEREST) {
            if (m->size() >= sizeof(InterestMessage)) {

                uint8_t q = m->quadrant();
                uint8_t last = _last_seen_quad.exchange(q);
                if(last != q) _cache->clear();

                const InterestMessage * im = reinterpret_cast<const InterestMessage *>(m->data());
                if (im->disinterest) {
                    _cache->on_disinterest(UNIT, m->address());
                    if (_on_disinterest) _on_disinterest(UNIT);
                } else {
                    _cache->on_interest(UNIT, m->address(), im->period_us);
                }
            }
            delete m; // tratado na hora
        } else if (_role == CONSUMER && h->kind == SmartHeader::RESPONSE) {
            // marca atividade: alimenta o refresh reativo (so reenvia no silencio)
            _last_response_ns.store(Clock::now_ns(), std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(_mtx);
                _producers.insert(mac_key(m->address()));
                ++_responses_received;
                // modo-valor: guarda sempre o ultimo valor recebido
                if (m->size() >= sizeof(ResponseMessage<Value>)) {
                    _last_value = reinterpret_cast<const ResponseMessage<Value> *>(m->data())->value;
                    _last_value_ns = Clock::now_ns();
                    _has_value = true;
                }
            }
            if (_value_mode) delete m;       // so o ultimo valor importa -> nao enfileira
            else Base::update(c, m);         // modo fila: enfileira a Message inteira (app drena)
        } else {
            delete m;
        }
    }

    // ===================== consumidor =====================
    Message * receive_response(int timeout_ms = -1) { return Base::updated(timeout_ms); }

    // modo-valor: o dado lido como uma variavel viva
    Value value() const { std::lock_guard<std::mutex> l(_mtx); return _last_value; }

    // operator Value(): trata o SmartData como o proprio dado
    operator Value() const { return value(); }

    // true se nunca recebeu ou se ja passou VALUE_TTL_FACTOR periodos sem atualizar
    bool expired() const {
        std::lock_guard<std::mutex> l(_mtx);
        if (!_has_value) return true;
        int64_t age = Clock::now_ns() - _last_value_ns;
        return age > static_cast<int64_t>(Smart_Config::VALUE_TTL_FACTOR) *
                     static_cast<int64_t>(_period_us) * 1000;
    }
    bool fresh() const { return !expired(); }

    std::size_t producer_count() const { std::lock_guard<std::mutex> l(_mtx); return _producers.size(); }
    uint64_t    response_count() const { std::lock_guard<std::mutex> l(_mtx); return _responses_received; }

    // n de reenvios reativos do interesse
    uint64_t    reissues() const { return _reissues.load(std::memory_order_relaxed); }

    void unsubscribe() {
        if (_role != CONSUMER || _unsubscribed) return;
        if (_refresh) _refresh->stop();
        _unsubscribed = true;
        for (int i = 0; i < 3; ++i) {           // 1 msg pode se perder
            send_interest(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // saida abrupta: para de reanunciar e nao envia desinteresse (simula um veiculo que some/cai)
    void abandon() {
        if (_role != CONSUMER) return;
        if (_refresh) _refresh->stop();
        _unsubscribed = true;  // impede o destrutor de enviar desinteresse
    }

    // ===================== produtor =====================
    void on_response_sent(std::function<void(uint64_t)> cb) { _on_response_sent = std::move(cb); }
    void on_disinterest_received(std::function<void(Unit)> cb) { _on_disinterest = std::move(cb); }
    uint64_t responses_sent() const { return _responses_sent.load(std::memory_order_relaxed); }

private:
    static const SmartHeader * header_of(Message * m) {
        if (m->size() < sizeof(SmartHeader)) return nullptr;
        return reinterpret_cast<const SmartHeader *>(m->data());
    }

    void respond(Unit /*UNIT*/) {
        ResponseMessage<Value> r;
        r.header.kind = SmartHeader::RESPONSE;
        r.header.unit = UNIT;
        r.value = _producer->produce(); // o componente gera o dado
        TypedMessage<ResponseMessage<Value>> msg(r);
        _comm->send(&msg);
        uint64_t n = _responses_sent.fetch_add(1, std::memory_order_relaxed) + 1;
        if (_on_response_sent) _on_response_sent(n);
    }

    void send_interest(bool disinterest) {
        InterestMessage im;
        im.header.kind = SmartHeader::INTEREST;
        im.header.unit = UNIT;
        im.period_us = _period_us;
        im.disinterest = disinterest ? 1 : 0;
        TypedMessage<InterestMessage> msg(im);
        _comm->send(&msg);
    }

    // Refresh reativo: reenvia o interesse so se passou INTEREST_REFRESH_US sem receber respostas
    void reactive_tick() {
        int64_t last = _last_response_ns.load(std::memory_order_relaxed);
        if (Clock::now_ns() - last >= static_cast<int64_t>(Smart_Config::INTEREST_REFRESH_US) * 1000) {
            _reissues.fetch_add(1, std::memory_order_relaxed);
            send_interest(false);
        }
    }

    Comm * _comm;
    Role   _role;

    // produtor
    IProducer<Value> * _producer = nullptr;
    std::unique_ptr<Binding_Cache> _cache;
    std::atomic<uint64_t> _responses_sent{0};
    std::atomic<uint8_t> _last_seen_quad{0xff};
    std::function<void(uint64_t)> _on_response_sent;
    std::function<void(Unit)> _on_disinterest;

    // consumidor
    uint64_t _period_us = 0;
    std::atomic<int64_t>  _last_response_ns{0}; // ultima Resposta recebida (refresh reativo)
    std::atomic<uint64_t> _reissues{0};         // reenvios reativos do interesse
    std::unique_ptr<Periodic_Thread> _refresh;
    bool _unsubscribed = false;
    mutable std::mutex _mtx;
    std::set<uint64_t> _producers;
    uint64_t _responses_received = 0;
    // modo-valor
    bool     _value_mode = false;
    Value    _last_value{};        // ultimo valor recebido (modo-valor)
    int64_t  _last_value_ns = 0;   // quando o ultimo valor chegou
    bool     _has_value = false;   // ja recebeu algum valor?
};

#endif

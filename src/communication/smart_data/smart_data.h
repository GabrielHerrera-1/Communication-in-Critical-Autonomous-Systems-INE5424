#ifndef SMART_DATA_H
#define SMART_DATA_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

#include "../../channel/vehicle_protocol.h"
#include "../../network/gps.h"
#include "../../core/clock.h"
#include "../../core/periodic_thread.h"
#include "../../core/observers/concurrent_observer.h"
#include "../communicator.h"
#include "../iproducer.h"
#include "../message/message.h"
#include "smart_message.h"
#include "unit.h"
#include "binding_cache.h"

struct Smart_Config {
    static constexpr uint64_t INTEREST_REFRESH_US     = 1'000'000; // reenvia interesse a cada 1s
    static constexpr uint64_t BINDING_LIFETIME_MIN_US = 3'500'000; // binding expira apos 3.5s sem refresh
    static constexpr unsigned BINDING_LIFETIME_FACTOR = 4;
    static constexpr uint64_t REAPER_PERIOD_US        = 1'000'000;
};

// SmartData<Tipo>: abstracao Interesse/Resposta (etapa 5). E um
// Concurrent_Observer<Message> que OBSERVA o Communicator: quando uma mensagem
// chega, o Communicator faz unmarshal e chama o update() abaixo com a Message
// inteira. Dois papeis pelo construtor:
//
//   PRODUTOR: recebe um IProducer<Value> (o componente produz o dado). Ao
//     receber um Interesse da sua Unit, registra o binding e dispara uma thread
//     periodica que responde. Trata o interesse no proprio update().
//   CONSUMIDOR: emite o Interesse (periodo) e ENFILEIRA as Respostas (a Message
//     inteira) na fila do Concurrent_Observer; a aplicacao drena via
//     receive_response()/get_value().
//
// Tipo e um descritor {static Unit UNIT; struct Value;} -- so o tipo.
//
// Restricao: UM SmartData por Communicator (o Communicator entrega a mesma
// Message a todos os observers da condicao; um componente e produtor OU
// consumidor, com seu proprio Communicator).
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

    // PRODUTOR
    SmartData(Comm * comm, IProducer<Value> * producer)
        : _comm(comm), _role(PRODUCER), _producer(producer) {
        _cache = std::make_unique<Binding_Cache>(
            [this](Unit u) { respond(u); },
            Smart_Config::BINDING_LIFETIME_MIN_US,
            Smart_Config::BINDING_LIFETIME_FACTOR,
            Smart_Config::REAPER_PERIOD_US);
        _comm->attach(this, _comm->broadcast_condition()); // observa o Communicator
    }

    // CONSUMIDOR
    SmartData(Comm * comm, uint64_t period_us, bool auto_refresh = true)
        : _comm(comm), _role(CONSUMER), _period_us(period_us ? period_us : 1) {
        _comm->attach(this, _comm->broadcast_condition());
        send_interest(false);
        if (auto_refresh) {
            _refresh = std::make_unique<Periodic_Thread>(
                Smart_Config::INTEREST_REFRESH_US, [this] { refresh_tick(); });
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
        drain_and_free(); // libera mensagens que ficaram na fila
    }

    // ===================== PUSH: o Communicator notifica =====================
    void update(Port c, Message * m) override {
        const SmartHeader * h = header_of(m);
        if (!h || h->unit != UNIT) { delete m; return; }

        if (_role == PRODUCER && h->kind == SmartHeader::INTEREST) {
            if (m->size() >= sizeof(InterestMessage)) {
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
            {
                std::lock_guard<std::mutex> lock(_mtx);
                _producers.insert(mac_key(m->address()));
                ++_responses_received;
            }
            Base::update(c, m); // ENFILEIRA a Message inteira (app drena)
        } else {
            delete m;
        }
    }

    // ===================== CONSUMIDOR (aplicacao) =====================
    // dequeue da proxima Resposta como Message inteira; nullptr no timeout.
    // O chamador assume a posse e deve dar delete.
    Message * receive_response(int timeout_ms = -1) { return Base::updated(timeout_ms); }

    // conveniencia: dequeue e devolve so o Value (libera a Message internamente).
    std::optional<Value> get_value(int timeout_ms = -1) {
        Message * m = Base::updated(timeout_ms);
        if (!m) return std::nullopt;
        std::optional<Value> v;
        if (m->size() >= sizeof(ResponseMessage<Value>))
            v = reinterpret_cast<const ResponseMessage<Value> *>(m->data())->value;
        delete m;
        return v;
    }

    std::size_t producer_count() const { std::lock_guard<std::mutex> l(_mtx); return _producers.size(); }
    uint64_t    response_count() const { std::lock_guard<std::mutex> l(_mtx); return _responses_received; }
    uint64_t    quadrant_suppressions() const { return _quadrant_suppressions.load(std::memory_order_relaxed); }

    void unsubscribe() {
        if (_role != CONSUMER || _unsubscribed) return;
        if (_refresh) _refresh->stop();
        _unsubscribed = true;
        for (int i = 0; i < 3; ++i) {           // 1 msg pode se perder
            send_interest(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // ===================== PRODUTOR =====================
    void on_response_sent(std::function<void(uint64_t)> cb) { _on_response_sent = std::move(cb); }
    void on_disinterest_received(std::function<void(Unit)> cb) { _on_disinterest = std::move(cb); }
    uint64_t responses_sent() const { return _responses_sent.load(std::memory_order_relaxed); }

private:
    static const SmartHeader * header_of(Message * m) {
        if (m->size() < sizeof(SmartHeader)) return nullptr;
        return reinterpret_cast<const SmartHeader *>(m->data());
    }

    static uint64_t mac_key(const Address & a) {
        const uint8_t * mac = a.paddr().raw();
        uint64_t k = 0;
        for (int i = 0; i < 6; ++i) k = (k << 8) | mac[i];
        return k;
    }

    void respond(Unit /*UNIT*/) {
        ResponseMessage<Value> r;
        r.header.kind = SmartHeader::RESPONSE;
        r.header.unit = UNIT;
        r.value = _producer->produce(); // o COMPONENTE gera o dado
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

    // refresh com consciencia de quadrante: ao trocar de quadrante, suprime o
    // reenvio neste ciclo (le o mesmo /dev/gps do gateway).
    void refresh_tick() {
        uint8_t qd = _gps.quadrant();
        uint8_t last = _last_quadrant.load(std::memory_order_relaxed);
        if (qd != GPS::QUADRANT_NONE && last != GPS::QUADRANT_NONE && qd != last) {
            _last_quadrant.store(qd, std::memory_order_relaxed);
            _quadrant_suppressions.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        _last_quadrant.store(qd, std::memory_order_relaxed);
        send_interest(false);
    }

    void drain_and_free() {
        Message * m;
        while ((m = Base::updated(0)) != nullptr) delete m;
    }

    Comm * _comm;
    Role   _role;

    // produtor
    IProducer<Value> * _producer = nullptr;
    std::unique_ptr<Binding_Cache> _cache;
    std::atomic<uint64_t> _responses_sent{0};
    std::function<void(uint64_t)> _on_response_sent;
    std::function<void(Unit)> _on_disinterest;

    // consumidor
    uint64_t _period_us = 0;
    GPS _gps;
    std::atomic<uint8_t>  _last_quadrant{GPS::QUADRANT_NONE};
    std::atomic<uint64_t> _quadrant_suppressions{0};
    std::unique_ptr<Periodic_Thread> _refresh;
    bool _unsubscribed = false;
    mutable std::mutex _mtx;
    std::set<uint64_t> _producers;
    uint64_t _responses_received = 0;
};

#endif

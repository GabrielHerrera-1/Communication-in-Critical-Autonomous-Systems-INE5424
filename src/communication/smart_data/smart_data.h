#ifndef SMART_DATA_H
#define SMART_DATA_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include "../../channel/vehicle_protocol.h"
#include "../../network/gps.h"
#include "../communicator.h"
#include "../iproducer.h"
#include "../message/message.h"
#include "../../core/clock.h"
#include "../../core/periodic_thread.h"
#include "smart_message.h"
#include "unit.h"
#include "binding_cache.h"

// Parametros de tempo do protocolo Interesse/Resposta.
struct Smart_Config {
    static constexpr uint64_t INTEREST_REFRESH_US     = 1'000'000; // 1s
    static constexpr uint64_t BINDING_LIFETIME_MIN_US = 3'500'000; // 3.5s
    static constexpr unsigned BINDING_LIFETIME_FACTOR = 4;
    static constexpr uint64_t REAPER_PERIOD_US        = 1'000'000; // 1s
};

// SmartData<Tipo>: a abstracao Interesse/Resposta time-triggered (Etapa 5).
//
// E um Communicator (Conditional_Data_Observer): quando o Protocol notifica,
// o update() abaixo le e interpreta a mensagem na hora -- modelo PUSH, sem
// thread/laco drenando o Communicator. Dois papeis pelo construtor:
//
//   RESPONSIVO (produtor): recebe um IProducer<Value> (o COMPONENTE produz o
//     dado). Ao receber um Interesse da sua Unit, registra o binding e dispara
//     uma thread periodica que envia Respostas.
//   INTERESSADO (consumidor): emite o Interesse (periodo) e mantem o ultimo
//     valor; operator Value() devolve. wait_for_responses() bloqueia ate chegar
//     N respostas (acordado pela CV no update(), sem polling).
//
// Tipo e um descritor {static Unit UNIT; using Value;} -- so o tipo, sem logica
// de producao (essa vive no componente, via IProducer).
template <typename Type>
class SmartData : public Communicator<Vehicle_Protocol> {
public:
    using Base    = Communicator<Vehicle_Protocol>;
    using Value   = typename Type::Value;
    using Address = Vehicle_Protocol::Address;
    using Port    = Vehicle_Protocol::Port;
    static constexpr Unit UNIT = Type::UNIT;

    // RESPONSIVO: o componente (IProducer) fornece o valor.
    SmartData(Vehicle_Protocol * channel, IProducer<Value> * producer, Port port)
        : Base(channel, port, /*subscribe_broadcast=*/true),
          _mode(RESPONSIVE),
          _producer(producer) {
        _cache = std::make_unique<Binding_Cache>(
            [this](Unit u) { respond(u); },
            Smart_Config::BINDING_LIFETIME_MIN_US,
            Smart_Config::BINDING_LIFETIME_FACTOR,
            Smart_Config::REAPER_PERIOD_US);
        this->attach_channel(); // so agora: membros prontos para o update()
    }

    // INTERESSADO: emite o Interesse (periodo) e guarda o ultimo valor.
    SmartData(Vehicle_Protocol * channel, uint64_t period_us, Port port,
              bool auto_refresh = true)
        : Base(channel, port, true),
          _mode(INTERESTED),
          _period_us(period_us ? period_us : 1) {
        this->attach_channel(); // pronto para receber Respostas
        send_interest(false);
        if (auto_refresh) {
            _refresh = std::make_unique<Periodic_Thread>(
                Smart_Config::INTEREST_REFRESH_US, [this] { refresh_tick(); });
        } else {
            // sem refresh proprio: reforca o envio inicial para a RSU captar
            for (int i = 0; i < 2; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                send_interest(false);
            }
        }
    }

    ~SmartData() override {
        this->detach_channel(); // para de receber ANTES de destruir membros
        if (_mode == INTERESTED) {
            if (_refresh) _refresh->stop();
            if (!_unsubscribed) send_interest(true); // best-effort desinteresse
            _refresh.reset();
        }
        _cache.reset(); // para as threads de resposta + reaper
    }

protected:
    // ===================== PUSH: notificacao do Protocol =====================
    // Override do update() do Communicator: NAO empilha na fila do
    // Concurrent_Observer; le e interpreta a mensagem (Interesse/Resposta) aqui.
    void update(typename Base::Condition /*c*/, typename Base::Buffer * buf) override {
        unsigned char payload[Vehicle_Protocol::MTU];
        Address from;
        int64_t ts = 0;
        uint8_t q = MessageHeader::QUADRANT_NONE;
        int size = this->_channel->receive(buf, &from, &ts, &q, payload, sizeof(payload));
        if (size < static_cast<int>(sizeof(SmartHeader))) return;

        const SmartHeader * h = reinterpret_cast<const SmartHeader *>(payload);
        if (h->unit != UNIT) return;

        if (_mode == RESPONSIVE && h->kind == SmartHeader::INTEREST) {
            if (size < static_cast<int>(sizeof(InterestMessage))) return;
            const InterestMessage * im = reinterpret_cast<const InterestMessage *>(payload);
            if (im->disinterest) {
                _cache->on_disinterest(UNIT);
                if (_on_disinterest) _on_disinterest(UNIT);
            } else {
                _cache->on_interest(UNIT, im->period_us);
            }
        } else if (_mode == INTERESTED && h->kind == SmartHeader::RESPONSE) {
            if (size < static_cast<int>(sizeof(ResponseMessage<Value>))) return;
            const ResponseMessage<Value> * rm =
                reinterpret_cast<const ResponseMessage<Value> *>(payload);
            {
                std::lock_guard<std::mutex> lock(_state_mtx);
                _value = rm->value;
                _value_ts = ts;
                _has_value = true;
                _producers.insert(mac_key(from));
                ++_responses_received;
            }
            _state_cv.notify_all();
            if (_on_response_received) _on_response_received(rm->value, Clock::now_ns());
        }
    }

public:
    // ===================== lado INTERESSADO =====================
    operator Value() const { return value(); }

    Value value() const {
        std::lock_guard<std::mutex> lock(_state_mtx);
        return _value;
    }

    bool has_value() const {
        std::lock_guard<std::mutex> lock(_state_mtx);
        return _has_value;
    }

    uint64_t response_count() const {
        std::lock_guard<std::mutex> lock(_state_mtx);
        return _responses_received;
    }

    std::size_t producer_count() const {
        std::lock_guard<std::mutex> lock(_state_mtx);
        return _producers.size();
    }

    uint64_t quadrant_suppressions() const {
        return _quadrant_suppressions.load(std::memory_order_relaxed);
    }

    // Callback por Resposta recebida (valor + instante de chegada local em ns),
    // invocado fora do lock. Util para medir o intervalo entre respostas.
    void on_response_received(std::function<void(const Value &, int64_t)> cb) {
        _on_response_received = std::move(cb);
    }

    // Bloqueia ate response_count >= target ou timeout. Sem polling: o update()
    // (push) acorda a condition_variable.
    bool wait_for_responses(uint64_t target, int timeout_ms) {
        std::unique_lock<std::mutex> lk(_state_mtx);
        return _state_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                  [&] { return _responses_received >= target; });
    }

    void unsubscribe() {
        if (_mode != INTERESTED || _unsubscribed) return;
        if (_refresh) _refresh->stop();
        _unsubscribed = true;
        for (int i = 0; i < 3; ++i) {
            send_interest(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // ===================== lado RESPONSIVO =====================
    void on_response_sent(std::function<void(uint64_t)> cb) {
        _on_response_sent = std::move(cb);
    }
    void on_disinterest_received(std::function<void(Unit)> cb) {
        _on_disinterest = std::move(cb);
    }
    uint64_t responses_sent() const {
        return _responses_sent.load(std::memory_order_relaxed);
    }

private:
    enum Mode { RESPONSIVE, INTERESTED };

    static uint64_t mac_key(const Address & a) {
        const uint8_t * m = a.paddr().raw();
        uint64_t k = 0;
        for (int i = 0; i < 6; ++i) k = (k << 8) | m[i];
        return k;
    }

    void respond(Unit /*UNIT*/) {
        ResponseMessage<Value> r;
        r.header.kind = SmartHeader::RESPONSE;
        r.header.unit = UNIT;
        r.value = _producer->produce(); // o COMPONENTE gera o dado
        TypedMessage<ResponseMessage<Value>> msg(r);
        this->send(&msg);
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
        this->send(&msg);
    }

    // Refresh com consciencia de quadrante (le o mesmo /dev/gps do gateway).
    // Ao trocar de quadrante, SUPRIME o reenvio neste ciclo.
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

    Mode _mode;

    // responsivo
    IProducer<Value> * _producer = nullptr;
    std::unique_ptr<Binding_Cache> _cache;
    std::atomic<uint64_t> _responses_sent{0};
    std::function<void(uint64_t)> _on_response_sent;
    std::function<void(Unit)> _on_disinterest;

    // interessado
    uint64_t _period_us = 0;
    GPS _gps;
    std::atomic<uint8_t>  _last_quadrant{GPS::QUADRANT_NONE};
    std::atomic<uint64_t> _quadrant_suppressions{0};
    std::unique_ptr<Periodic_Thread> _refresh;
    bool _unsubscribed = false;
    mutable std::mutex _state_mtx;
    std::condition_variable _state_cv;
    Value    _value{};
    int64_t  _value_ts = 0;
    bool     _has_value = false;
    std::set<uint64_t> _producers;
    uint64_t _responses_received = 0;
    std::function<void(const Value &, int64_t)> _on_response_received;
};

#endif

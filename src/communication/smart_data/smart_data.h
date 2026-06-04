#ifndef SMART_DATA_H
#define SMART_DATA_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <chrono>
#include <utility>

#include "../../channel/vehicle_protocol.h"
#include "../../network/gps.h"
#include "../communicator.h"
#include "../message/message.h"
#include "../../core/clock.h"
#include "../../core/periodic_thread.h"
#include "smart_message.h"
#include "unit.h"
#include "binding_cache.h"

// Parametros de tempo do protocolo Interesse/Resposta.
struct Smart_Config {
    // subscriber reenvia o interesse periodicamente: cobre perda do interesse e
    // late joiners (responsivos que entram depois), e mantem o soft-state vivo.
    static constexpr uint64_t INTEREST_REFRESH_US     = 1'000'000; // 1s
    // responsivo expira um binding se ficar este tempo sem refresh (ou
    // LIFETIME_FACTOR * periodo, o que for maior): cobre subscriber que caiu.
    static constexpr uint64_t BINDING_LIFETIME_MIN_US = 3'500'000; // 3.5s
    static constexpr unsigned BINDING_LIFETIME_FACTOR = 4;
    static constexpr uint64_t REAPER_PERIOD_US        = 1'000'000; // 1s
};

// SmartData<Transducer>: a abstracao Interesse/Resposta time-triggered da etapa
// 5, no espirito do SmartData do EPOS. Uma mesma classe, parametrizada pelo tipo
// do dado (via Transducer), com DOIS papeis selecionados pelo construtor:
//
//   RESPONSIVO (produtor): liga um Transducer local. Ao receber um Interesse da
//     sua Unit, registra o binding na cache e dispara uma thread periodica que
//     envia Respostas indefinidamente (ate desinteresse/expiry). serve() roda o
//     laco de recepcao de interesses em foreground.
//
//   INTERESSADO (consumidor): emite o Interesse (e o reenvia periodicamente) e
//     mantem o ultimo valor recebido. update_once() bloqueia ate a proxima
//     Resposta; operator Value() devolve o ultimo valor.
//
// Roteamento: tudo em broadcast (logical_broadcast do Communicator). A
// discriminacao por tipo e natureza e feita filtrando kind/unit no payload --
// origin e timestamp vem do Protocol::Header de graca. (Uma otimizacao possivel
// seria uma porta por Unit; mantivemos broadcast global + filtro por simplicidade.)
//
// Threads: o rx fica em FOREGROUND (serve()/update_once(), dirigidos pelo
// componente); apenas threads SEND-ONLY ficam em background (respostas
// periodicas no responsivo, refresh do interesse no interessado). Como o
// Semaphore do projeto nao tem wait com timeout, isto evita ter de "acordar" um
// rx bloqueado no shutdown -- as threads de background dormem em fatias e sao
// joinadas de forma limpa.
template <typename Transducer>
class SmartData {
public:
    using Value   = typename Transducer::Value;
    using Comm    = Communicator<Vehicle_Protocol>;
    using Address = Vehicle_Protocol::Address;
    static constexpr Unit UNIT = Transducer::UNIT;

    // ---- RESPONSIVO ----
    SmartData(Transducer transducer, Comm * comm)
        : _mode(RESPONSIVE),
          _comm(comm),
          _running(true),
          _transducer(std::move(transducer)) {
        _cache = std::make_unique<Binding_Cache>(
            [this](Unit u) { respond(u); },
            Smart_Config::BINDING_LIFETIME_MIN_US,
            Smart_Config::BINDING_LIFETIME_FACTOR,
            Smart_Config::REAPER_PERIOD_US);
    }

    // ---- INTERESSADO ----
    SmartData(Comm * comm, uint64_t period_us, bool auto_refresh = true)
        : _mode(INTERESTED),
          _comm(comm),
          _running(true),
          _period_us(period_us ? period_us : 1) {
        send_interest(false); // primeiro interesse imediato
        if (auto_refresh) {
            _refresh = std::make_unique<Periodic_Thread>(
                Smart_Config::INTEREST_REFRESH_US, [this] { refresh_tick(); });
        } else {
            // sem refresh proprio: reforca o envio inicial algumas vezes para
            // que a RSU capte o interesse e passe a repeti-lo no nosso lugar
            // (rastreamento passivo). Cobre o caso de delegar a repeticao a RSU.
            for (int i = 0; i < 2; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                send_interest(false);
            }
        }
    }

    SmartData(const SmartData &) = delete;
    SmartData & operator=(const SmartData &) = delete;

    ~SmartData() {
        _running.store(false, std::memory_order_release);
        if (_mode == INTERESTED) {
            if (_refresh) _refresh->stop();
            if (!_unsubscribed) send_interest(true); // best-effort desinteresse
            _refresh.reset();
        }
        _cache.reset(); // responsivo: para threads de resposta + reaper
    }

    // ===================== lado RESPONSIVO =====================

    // Laco foreground de recepcao de interesses. Nao retorna: o produtor
    // responde "indefinidamente" enquanto a aplicacao vive (spec).
    void serve() {
        Message raw;
        while (_running.load(std::memory_order_acquire)) {
            if (!_comm->receive(&raw)) continue;
            const SmartHeader * h = header_of(raw);
            if (!h || h->unit != UNIT || h->kind != SmartHeader::INTEREST) continue;
            if (raw.size() < sizeof(InterestMessage)) continue;

            const InterestMessage * im =
                reinterpret_cast<const InterestMessage *>(raw.data());
            if (im->disinterest) {
                _cache->on_disinterest(UNIT);
                if (_on_disinterest) _on_disinterest(UNIT);
            } else {
                _cache->on_interest(UNIT, im->period_us);
            }
        }
    }

    // Callback invocado (na thread de resposta) apos cada Resposta enviada.
    // Deve ser configurado ANTES de serve(). Util para testes imprimirem
    // marcos sem acoplar a logica de teste ao SmartData.
    void on_response_sent(std::function<void(uint64_t)> cb) {
        _on_response_sent = std::move(cb);
    }

    // Callback invocado (na thread de serve) quando um desinteresse e processado.
    // Permite ao produtor registrar/anunciar que parou de responder aquela Unit.
    void on_disinterest_received(std::function<void(Unit)> cb) {
        _on_disinterest = std::move(cb);
    }

    uint64_t responses_sent() const {
        return _responses_sent.load(std::memory_order_relaxed);
    }

    // ===================== lado INTERESSADO =====================

    // Bloqueia ate a proxima Resposta da Unit; atualiza o estado interno e,
    // opcionalmente, devolve valor/origem/timestamp da resposta.
    bool update_once(Value * out = nullptr, Address * from = nullptr,
                     int64_t * ts = nullptr) {
        Message raw;
        while (_running.load(std::memory_order_acquire)) {
            if (!_comm->receive(&raw)) continue;
            const SmartHeader * h = header_of(raw);
            if (!h || h->unit != UNIT || h->kind != SmartHeader::RESPONSE) continue;
            if (raw.size() < sizeof(ResponseMessage<Value>)) continue;

            const ResponseMessage<Value> * rm =
                reinterpret_cast<const ResponseMessage<Value> *>(raw.data());
            Address src = raw.address();
            int64_t  t  = raw.timestamp();
            {
                std::lock_guard<std::mutex> lock(_rx_mtx);
                _value = rm->value;
                _value_ts = t;
                _has_value = true;
                _producers.insert(mac_key(src));
                ++_responses_received;
            }
            if (out)  *out  = rm->value;
            if (from) *from = src;
            if (ts)   *ts   = t;
            return true;
        }
        return false;
    }

    operator Value() const { return value(); }

    Value value() const {
        std::lock_guard<std::mutex> lock(_rx_mtx);
        return _value;
    }

    bool has_value() const {
        std::lock_guard<std::mutex> lock(_rx_mtx);
        return _has_value;
    }

    uint64_t response_count() const {
        std::lock_guard<std::mutex> lock(_rx_mtx);
        return _responses_received;
    }

    std::size_t producer_count() const {
        std::lock_guard<std::mutex> lock(_rx_mtx);
        return _producers.size();
    }

    // Quantas vezes o refresh do interesse foi SUPRIMIDO por troca de quadrante
    // (so tem efeito com GPS disponivel). Demonstra a anotacao "se alguem vai
    // ficar reenviando interesse, para de enviar na troca de quadrante".
    uint64_t quadrant_suppressions() const {
        return _quadrant_suppressions.load(std::memory_order_relaxed);
    }

    // Desinteresse explicito (o "bit de desinteresse" da spec): para o refresh e
    // anuncia o cancelamento. Usado quando o veiculo sai da simulacao.
    void unsubscribe() {
        if (_mode != INTERESTED || _unsubscribed) return;
        if (_refresh) _refresh->stop();
        _unsubscribed = true;
        // reenvia o desinteresse algumas vezes: uma mensagem so pode se perder
        // (spec). Mesmo que todas se percam, o soft-state do produtor expira.
        for (int i = 0; i < 3; ++i) {
            send_interest(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

private:
    enum Mode { RESPONSIVE, INTERESTED };

    static const SmartHeader * header_of(Message & raw) {
        if (raw.size() < sizeof(SmartHeader)) return nullptr;
        return reinterpret_cast<const SmartHeader *>(raw.data());
    }

    static uint64_t mac_key(const Address & a) {
        const uint8_t * m = a.paddr().raw();
        uint64_t k = 0;
        for (int i = 0; i < 6; ++i) k = (k << 8) | m[i];
        return k;
    }

    // gera e envia UMA resposta (chamado pela thread periodica do binding).
    void respond(Unit /*unit == UNIT*/) {
        ResponseMessage<Value> r;
        r.header.kind = SmartHeader::RESPONSE;
        r.header.unit = UNIT;
        r.value = _transducer.sense(); // "pede para gerar o dado"
        TypedMessage<ResponseMessage<Value>> msg(r);
        _comm->send(&msg); // broadcast; o Communicator carimba o timestamp
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

    // Refresh com consciencia de quadrante. Ao detectar troca de quadrante,
    // SUPRIME o reenvio do interesse neste ciclo: deixa os bindings do quadrante
    // antigo expirarem e reissua naturalmente ja no novo quadrante. Sem GPS
    // (QUADRANT_NONE), comporta-se como um refresh comum.
    void refresh_tick() {
        uint8_t q = _gps.quadrant();
        uint8_t last = _last_quadrant.load(std::memory_order_relaxed);
        if (q != GPS::QUADRANT_NONE && last != GPS::QUADRANT_NONE && q != last) {
            _last_quadrant.store(q, std::memory_order_relaxed);
            _quadrant_suppressions.fetch_add(1, std::memory_order_relaxed);
            return; // troca de quadrante: nao reenvia o interesse
        }
        _last_quadrant.store(q, std::memory_order_relaxed);
        send_interest(false);
    }

    Mode  _mode;
    Comm * _comm;
    std::atomic<bool> _running;

    // responsivo
    Transducer _transducer{};
    std::unique_ptr<Binding_Cache> _cache;
    std::atomic<uint64_t> _responses_sent{0};
    std::function<void(uint64_t)> _on_response_sent;
    std::function<void(Unit)> _on_disinterest;

    // interessado
    uint64_t _period_us = 0;
    GPS _gps;                                            // consulta o quadrante atual
    std::atomic<uint8_t>  _last_quadrant{GPS::QUADRANT_NONE};
    std::atomic<uint64_t> _quadrant_suppressions{0};
    std::unique_ptr<Periodic_Thread> _refresh;
    bool _unsubscribed = false;
    mutable std::mutex _rx_mtx;
    Value    _value{};
    int64_t  _value_ts = 0;
    bool     _has_value = false;
    std::set<uint64_t> _producers;
    uint64_t _responses_received = 0;
};

#endif

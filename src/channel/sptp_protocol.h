#ifndef SPTP_PROTOCOL_H
#define SPTP_PROTOCOL_H

#include "packet_kind.h"
#include "../core/clock.h"
#include "../core/traits.h"
#include "../application/component_ports.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <type_traits>
#include <utility>

// sptp: simplificacao do ptp com 4 timestamps
// - roda so no gateway (Protocol so instancia quando existe socket_nic)
// - ajusta CLOCK_REALTIME via clock_settime, conforme orientado pelo professor
//
//   offset = ((t1' - t1) - (t2' - t2)) / 2
//   delay  = ((t1' - t1) + (t2' - t2)) / 2
//
// TODO: verificar se o unicast ta de boa pra sptp

// se n fosse assim teriamos dependencia circular
template <typename Address>
class SPTP_Protocol {
public:
    // ponteiro pra send do protocol. justamente pra evitar dependencia circular.
    using SendFn = int(*)(Address from, Address to, const void* data, unsigned int size, int64_t ts, PacketKind kind);

    SPTP_Protocol(Address own_addr, bool is_master, SendFn send_fn)
        : _own_addr(own_addr),
          _is_master(is_master),
          _send(send_fn),
          _current_delay_ns(Cfg::INITIAL_DELAY_NS),
          _current_offset_ns(0),
          _pending_t2_ns(0),
          _pending_seq(0),
          _next_seq(1),
          _last_sync_steady_ns(0),
          _max_silence_s(load_max_silence()),
          _running(false) {}

    ~SPTP_Protocol() { stop(); }

    // resync periodica, so no slave. thread vigia ha quanto tempo nao recebe sync, se passar de _max_silence_s,
    // dispara um novo round-trip. slave tambem pede sync imediata ao ligar
    void start() {
        if (_is_master) return;

        _last_sync_steady_ns.store(steady_now_ns(), std::memory_order_release);
        _running.store(true, std::memory_order_release);

        // sync logo na inicializacao: sem isso, o slave espera _max_silence_s
        // antes de pedir a primeira sincronizacao
        send_sync_request();

        _silence_worker = std::thread([this]() {
            using namespace std::chrono;
            auto sleep_for = duration<double>(_max_silence_s);
            while (_running.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(sleep_for);

                int64_t now  = steady_now_ns();
                int64_t last = _last_sync_steady_ns.load(std::memory_order_acquire);
                double elapsed_s = static_cast<double>(now - last) / 1e9;

                if (elapsed_s > _max_silence_s && _running.load(std::memory_order_acquire)) {
                    send_sync_request();
                }
            }
        });
    }

    void stop() {
        _running.store(false, std::memory_order_release);
        if (_silence_worker.joinable()) {
            _silence_worker.join();
        }
    }

    // chamado pelo Protocol quando chega um pacote sptp
    // header_ts = packet->timestamp() (t2 quando REQUEST_SYNC, t1 quando SYNC). TODO: ver se isso é ok
    // payload = conteudo apos o Header do Protocol (ptp_frame):
    //    - REQUEST_SYNC: Request_Payload com seq_id
    //    - SYNC: Sync_Payload com requester_mac + seq_id + t2'
    void on_receive(PacketKind kind, Address sender_addr, int64_t header_ts, const void* payload, unsigned int payload_size) {
        if (_is_master) {
            if (kind == PacketKind::SPTP_REQUEST_SYNC) {
                if (payload_size < sizeof(Request_Payload)) return;

                Request_Payload req;
                std::memcpy(&req, payload, sizeof(req));

                // marca t2' o mais cedo possivel
                int64_t t2_prime = Clock::now_ns();
                send_sync_reply(sender_addr, req.seq_id, t2_prime);
            }
            return;
        }

        // slave so processa sync vinda do master conhecido
        if (kind != PacketKind::SPTP_SYNC) return;
        if (payload_size < sizeof(Sync_Payload)) return;

        // aq slave recebeu o sync
        int64_t t1_prime = Clock::now_ns();

        // timestamp que veio no header do sync
        int64_t t1       = header_ts;

        // qnd o slave mandou o request sync
        int64_t t2       = _pending_t2_ns.load(std::memory_order_acquire);
        if (t2 == 0) return;

        Sync_Payload p;
        std::memcpy(&p, payload, sizeof(p));

        // SYNC ja chega unicast (kernel filtra por MAC destino antes de
        // entregar ao raw socket), entao nao precisamos do MAC dentro do
        // payload. seq_id é usado pra sincronicidade, ver se n é antigo
        if (p.seq_id != _pending_seq.load(std::memory_order_acquire)) return;

        // qnd o master recebeu o request
        int64_t t2_prime = p.t2_prime;

        int64_t raw_offset = ((t1_prime - t1) - (t2_prime - t2)) / 2;
        int64_t raw_delay  = ((t1_prime - t1) + (t2_prime - t2)) / 2;

        _last_sync_steady_ns.store(steady_now_ns(), std::memory_order_release);

        // EWMA sobre o delay medido
        // serve pra basicamente preservar boa parte do valor antigo, incorporando um pouco da medicao nova. delay mais estavel 
        if (raw_delay > 0) {
            _current_delay_ns = static_cast<int64_t>(
                (1.0 - Cfg::ALPHA) * _current_delay_ns + Cfg::ALPHA * raw_delay
            );
        }

        int64_t abs_offset = std::llabs(raw_offset);
        if (abs_offset > Cfg::MIN_OFFSET_NS && abs_offset < Cfg::MAX_OFFSET_NS) {
            // aqui a gente cancela o erro. le clock realtime, soma esse delta e reescreve o relogio com clock settime
            adjust_clock_jump(-raw_offset);
        }
        _current_offset_ns = raw_offset;
        // TODO: rever comportamento do fetch_add
        int count = _sync_count.fetch_add(1, std::memory_order_relaxed) + 1;

        // marca no log que a sincronizacao foi aplicada. imprime sempre na
        // primeira aplicacao e depois uma vez a cada 5 para nao poluir
        // TODO: ver se esse print n ta adicionando tempo desnecessario
        if (count == 1 || (count % 5) == 0) {
            std::fprintf(stdout,
                "[SPTP] sync #%d aplicada: offset=%lld ns delay=%lld ns\n",
                count,
                static_cast<long long>(raw_offset),
                static_cast<long long>(_current_delay_ns));
            std::fflush(stdout);
        }

        // libera o t2 guardado; proximo request produzira um novo
        _pending_t2_ns.store(0, std::memory_order_release);
    }

    // usado nos testes
    int64_t current_delay_ns()  const { return _current_delay_ns; }
    int64_t current_offset_ns() const { return _current_offset_ns; }
    int     sync_count()        const { return _sync_count.load(std::memory_order_relaxed); }

private:
    using Cfg = Traits<SPTP_Protocol<Address>>;
    using Physical_Address =
        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Address>().paddr())>>;

    struct Request_Payload {
        uint32_t seq_id;
    } __attribute__((packed));

    struct Sync_Payload {
        uint32_t seq_id;
        int64_t  t2_prime;
    } __attribute__((packed));

    // permite override em tempo de execucao via env var
    // valor em segundos; se ausente ou <= 0, usa o default da traits
    static double load_max_silence() {
        if (const char * raw = std::getenv("SO2_SPTP_MAX_SILENCE_S")) {
            double v = std::atof(raw);
            if (v > 0.0) return v;
        }
        return Cfg::MAX_SILENCE_S;
    }

    void adjust_clock_jump(int64_t delta_ns) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        int64_t total = static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL
                      + ts.tv_nsec + delta_ns;
        ts.tv_sec  = total / 1'000'000'000LL;
        ts.tv_nsec = total % 1'000'000'000LL;
        if (clock_settime(CLOCK_REALTIME, &ts) < 0) {
            perror("[SPTP] clock_settime");
        }
    }

    void send_sync_request() {
        uint32_t seq = _next_seq.fetch_add(1, std::memory_order_relaxed);
        int64_t t2 = Clock::now_ns();
        _pending_t2_ns.store(t2, std::memory_order_release);
        _pending_seq.store(seq, std::memory_order_release);
        Request_Payload p{ seq };
        // ts = 0: deixa o Protocol carimbar com now_ns na saida da NIC. O
        // valor que vai no Header e ignorado pelo master, so existe pra
        // cumprir o formato PTP = {address, timestamp, ptp_frame}. TODO: ver se é necessario ter ts aq memso, se for, acho q devemos usar o monotonic aq
        _send(_own_addr, Address::physical_broadcast(Component_Ports::PTP), &p, sizeof(p), 0,
              PacketKind::SPTP_REQUEST_SYNC);
    }

    void send_sync_reply(Address to, uint32_t seq_id, int64_t t2_prime) {
        Sync_Payload p{};
        p.seq_id = seq_id;
        p.t2_prime = t2_prime;
        // passa ts = 0 para que o Protocol meca t1 com now_ns no ponto mais
        // proximo da NIC. O slave lera esse t1 do Header.timestamp.
        _send(_own_addr, to, &p, sizeof(p), 0, PacketKind::SPTP_SYNC);
    }

    // steady_clock em ns desde o epoch do proprio steady_clock. Usado pelo
    // watchdog para ser imune a clock_settime feito pelo proprio SPTP. TODO: ver se é necessario msm
    static int64_t steady_now_ns() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()).count();
    }

    Address _own_addr;
    bool    _is_master;
    SendFn  _send;

    int64_t _current_delay_ns;
    int64_t _current_offset_ns;
    std::atomic<int64_t>  _pending_t2_ns;       // t2 do REQUEST_SYNC pendente
    std::atomic<uint32_t> _pending_seq;         // seq_id do REQUEST_SYNC pendente
    std::atomic<uint32_t> _next_seq;            // proxima seq_id a emitir
    std::atomic<int64_t>  _last_sync_steady_ns; // steady_clock na ultima aplicacao
    double  _max_silence_s;              // periodo da ressincronizacao automatica

    std::atomic<bool> _running;
    std::thread       _silence_worker;
    std::atomic<int>  _sync_count{0};
};

#endif

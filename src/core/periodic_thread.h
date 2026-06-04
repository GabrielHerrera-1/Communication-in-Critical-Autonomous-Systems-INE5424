#ifndef PERIODIC_THREAD_H
#define PERIODIC_THREAD_H

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

// Thread que executa um job a cada periodo, indefinidamente, ate stop().
//
// E a primitiva que a spec pede no lado responsivo: "quando um interesse chega,
// dar deploy numa nova thread periodica que envia as respostas". Generaliza o
// padrao ad-hoc que ja existia no watchdog do SPTP.
//
// Detalhes:
//   - deadlines ABSOLUTOS (steady_clock): o periodo nao acumula o tempo gasto
//     dentro do job nem o drift do sleep. "Nao precisa ser perfeito" (spec),
//     mas isto e barato e mantem o ritmo estavel.
//   - sleep em fatias curtas checando _running: stop()/destrutor retornam
//     rapido mesmo com periodos grandes, sem precisar de wait com timeout.
//   - set_period() permite reajustar o intervalo em runtime (usado quando um
//     novo interesse pede um periodo menor).
class Periodic_Thread {
public:
    Periodic_Thread(uint64_t period_us, std::function<void()> job)
        : _period_us(period_us),
          _job(std::move(job)),
          _running(true),
          _thread([this] { loop(); }) {}

    // nao copiavel nem movivel: a thread captura o this.
    Periodic_Thread(const Periodic_Thread &) = delete;
    Periodic_Thread & operator=(const Periodic_Thread &) = delete;

    ~Periodic_Thread() { stop(); }

    void stop() {
        _running.store(false, std::memory_order_release);
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    void set_period(uint64_t period_us) {
        _period_us.store(period_us, std::memory_order_relaxed);
    }

private:
    using clock = std::chrono::steady_clock;

    void loop() {
        auto next = clock::now();
        while (_running.load(std::memory_order_acquire)) {
            _job();

            uint64_t period = _period_us.load(std::memory_order_relaxed);
            next += std::chrono::microseconds(period);

            // se ficamos para tras (job lento / periodo curto), reancora em now
            // para nao entrar em busy-loop tentando recuperar o atraso.
            auto now = clock::now();
            if (next < now) {
                next = now + std::chrono::microseconds(period);
            }

            // dorme em fatias de ate 50ms para reagir rapido ao stop().
            while (_running.load(std::memory_order_acquire) && clock::now() < next) {
                auto slice = next - clock::now();
                auto cap = std::chrono::milliseconds(50);
                std::this_thread::sleep_for(slice < cap ? slice : cap);
            }
        }
    }

    std::atomic<uint64_t> _period_us;
    std::function<void()> _job;
    std::atomic<bool>     _running;
    std::thread           _thread;
};

#endif

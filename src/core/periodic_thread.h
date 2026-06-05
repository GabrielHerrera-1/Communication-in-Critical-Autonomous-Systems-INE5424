#ifndef PERIODIC_THREAD_H
#define PERIODIC_THREAD_H

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

// Thread que executa um job a cada periodo, ate stop(). E a primitiva que a
// spec pede no responsivo: "uma nova thread periodica que envia as respostas".
//
//   - deadlines ABSOLUTOS (steady_clock): o periodo nao acumula o tempo do job
//     nem o drift do sleep.
//   - sleep em fatias curtas checando _running: stop() retorna rapido.
//   - set_period() reajusta o intervalo em runtime.
class Periodic_Thread {
public:
    Periodic_Thread(uint64_t period_us, std::function<void()> job)
        : _period_us(period_us), _job(std::move(job)),
          _running(true), _thread([this] { loop(); }) {}

    Periodic_Thread(const Periodic_Thread &) = delete;
    Periodic_Thread & operator=(const Periodic_Thread &) = delete;

    ~Periodic_Thread() { stop(); }

    void stop() {
        _running.store(false, std::memory_order_release);
        if (_thread.joinable()) _thread.join();
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
            auto now = clock::now();
            if (next < now) next = now + std::chrono::microseconds(period);
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

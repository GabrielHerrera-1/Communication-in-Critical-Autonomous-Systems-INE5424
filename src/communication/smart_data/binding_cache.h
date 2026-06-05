#ifndef BINDING_CACHE_H
#define BINDING_CACHE_H

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "unit.h"
#include "../../core/clock.h"
#include "../../core/periodic_thread.h"

// Cache de bindings do lado produtor. Mapeia Unit -> interesse ativo. Algoritmo
// das anotacoes: ao chegar um interesse, se a Unit NAO esta na cache cria-se a
// thread periodica que responde; se JA esta, atualiza (periodo + validade).
//
// Como as respostas sao broadcast, UMA thread por Unit serve todos os
// interessados. Soft-state: cada binding tem validade (expiry); o reenvio do
// interesse a renova; um reaper remove bindings expirados (subscriber que sumiu
// sem desinteresse). O desinteresse explicito remove na hora.
class Binding_Cache {
public:
    Binding_Cache(std::function<void(Unit)> respond,
                  uint64_t lifetime_min_us, unsigned lifetime_factor,
                  uint64_t reaper_period_us)
        : _respond(std::move(respond)),
          _lifetime_min_us(lifetime_min_us), _lifetime_factor(lifetime_factor),
          _running(true),
          _reaper([this, reaper_period_us] { reap_loop(reaper_period_us); }) {}

    ~Binding_Cache() {
        _running.store(false, std::memory_order_release);
        if (_reaper.joinable()) _reaper.join();
        std::lock_guard<std::mutex> lock(_mtx);
        _bindings.clear();
    }

    void on_interest(Unit unit, uint64_t period_us) {
        if (period_us == 0) period_us = 1;
        std::lock_guard<std::mutex> lock(_mtx);
        auto it = _bindings.find(unit);
        if (it == _bindings.end()) {                    // nao esta na cache: cria
            Binding b;
            b.period_us = period_us;
            b.expiry_ns = expiry_for(period_us);
            auto respond = _respond;
            b.thread = std::make_unique<Periodic_Thread>(
                period_us, [respond, unit] { respond(unit); });
            _bindings.emplace(unit, std::move(b));
        } else {                                        // ja esta: atualiza
            Binding & b = it->second;
            if (period_us < b.period_us) {
                b.period_us = period_us;
                b.thread->set_period(period_us);
            }
            b.expiry_ns = expiry_for(b.period_us);
        }
    }

    void on_disinterest(Unit unit) {
        std::lock_guard<std::mutex> lock(_mtx);
        _bindings.erase(unit);
    }

private:
    struct Binding {
        uint64_t period_us = 0;
        int64_t  expiry_ns = 0;
        std::unique_ptr<Periodic_Thread> thread;
    };

    int64_t expiry_for(uint64_t period_us) const {
        uint64_t life = _lifetime_factor * period_us;
        if (life < _lifetime_min_us) life = _lifetime_min_us;
        return Clock::now_ns() + static_cast<int64_t>(life) * 1000;
    }

    void reap_loop(uint64_t reaper_period_us) {
        using namespace std::chrono;
        while (_running.load(std::memory_order_acquire)) {
            uint64_t slept = 0;
            while (_running.load(std::memory_order_acquire) && slept < reaper_period_us) {
                uint64_t step = reaper_period_us - slept;
                if (step > 50000) step = 50000;
                std::this_thread::sleep_for(microseconds(step));
                slept += step;
            }
            if (!_running.load(std::memory_order_acquire)) break;
            int64_t now = Clock::now_ns();
            std::lock_guard<std::mutex> lock(_mtx);
            for (auto it = _bindings.begin(); it != _bindings.end();) {
                if (now > it->second.expiry_ns) it = _bindings.erase(it);
                else ++it;
            }
        }
    }

    std::function<void(Unit)> _respond;
    uint64_t _lifetime_min_us;
    unsigned _lifetime_factor;
    std::map<Unit, Binding> _bindings;
    std::mutex _mtx;
    std::atomic<bool> _running;
    std::thread _reaper;
};

#endif

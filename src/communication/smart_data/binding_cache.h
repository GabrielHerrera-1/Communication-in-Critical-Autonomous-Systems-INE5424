#ifndef BINDING_CACHE_H
#define BINDING_CACHE_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "unit.h"
#include "../../channel/vehicle_protocol.h"
#include "../../core/clock.h"
#include "../../core/periodic_thread.h"

// Cache de bindings do lado produtor. Agora cada registro guarda o ENDERECO de
// quem demonstrou interesse: assim um desinteresse de um agente remove apenas o
// registro DELE, sem derrubar os outros interessados na mesma Unit.
//
// Os bindings ficam num std::vector (um registro por (Unit, Address)); a busca
// e feita filtrando o vetor. Numa "colisao" de Unit (varios interessados) nada
// e descartado -- todos os registros ficam. Como as respostas sao broadcast,
// mantemos UMA Periodic_Thread por Unit, que responde no periodo MAIS CURTO
// entre os interessados daquela Unit (ver shortest_locked()).
//
// Soft-state: cada registro tem validade (expiry); o reenvio do interesse a
// renova; um reaper remove registros expirados. O desinteresse explicito remove
// na hora.
class Binding_Cache {
public:
    using Address = Vehicle_Protocol::Address;

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
        _threads.clear();
        _bindings.clear();
    }

    // Interesse de 'addr' na 'unit' com 'period_us'. Se ja existe um registro
    // desse mesmo addr para a unit, atualiza periodo/validade; senao adiciona.
    void on_interest(Unit unit, const Address & addr, uint64_t period_us) {
        if (period_us == 0) period_us = 1;
        std::lock_guard<std::mutex> lock(_mtx);
        Binding * b = find_binding(unit, addr);
        if (b) {
            b->period_us = period_us;
            b->expiry_ns = expiry_for(period_us);
        } else {
            _bindings.push_back({unit, addr, period_us, expiry_for(period_us)});
        }
        resync_threads();
    }

    // Desinteresse de 'addr': remove apenas os registros dele para a unit. Os
    // demais interessados continuam sendo atendidos.
    void on_disinterest(Unit unit, const Address & addr) {
        std::lock_guard<std::mutex> lock(_mtx);
        _bindings.erase(
            std::remove_if(_bindings.begin(), _bindings.end(),
                [&](const Binding & b) { return b.unit == unit && b.address == addr; }),
            _bindings.end());
        resync_threads();
    }

    // Limpa imediatamente todos os registros de interesse (bindings)
    // e encerra todas as threads periodicas de transmissao associadas.
    void clear() {
        std::lock_guard<std::mutex> lock(_mtx);
        _bindings.clear();
        _threads.clear(); // Destroi as threads, o que deve interromper sua execucao
    }

private:
    struct Binding {
        Unit     unit;
        Address  address;
        uint64_t period_us;
        int64_t  expiry_ns;
    };

    // Filtra o vetor de bindings (assume _mtx travado).
    template <typename Pred>
    std::vector<Binding *> filter(Pred pred) {
        std::vector<Binding *> out;
        for (auto & b : _bindings)
            if (pred(b)) out.push_back(&b);
        return out;
    }

    // Primeiro registro de (unit, addr), ou nullptr (assume _mtx travado).
    Binding * find_binding(Unit unit, const Address & addr) {
        auto v = filter([&](const Binding & b) {
            return b.unit == unit && b.address == addr;
        });
        return v.empty() ? nullptr : v.front();
    }

    // Menor period_us entre os interessados da unit; 0 se nenhum (assume travado).
    uint64_t shortest_locked(Unit unit) {
        uint64_t best = 0;
        for (Binding * b : filter([&](const Binding & x) { return x.unit == unit; }))
            if (best == 0 || b->period_us < best) best = b->period_us;
        return best;
    }

    // Garante uma thread por Unit ativa, no periodo mais curto; remove as threads
    // de Units que ficaram sem interessados (assume _mtx travado).
    void resync_threads() {
        // cria/atualiza para cada unit presente nos bindings
        for (auto & b : _bindings) {
            uint64_t sp = shortest_locked(b.unit);
            auto it = _threads.find(b.unit);
            if (it == _threads.end()) {
                auto respond = _respond;
                Unit u = b.unit;
                _threads.emplace(b.unit, std::make_unique<Periodic_Thread>(
                    sp, [respond, u] { respond(u); }));
            } else {
                it->second->set_period(sp);
            }
        }
        // remove threads de units que nao tem mais interessados
        for (auto it = _threads.begin(); it != _threads.end();) {
            if (shortest_locked(it->first) == 0) it = _threads.erase(it);
            else ++it;
        }
    }

    int64_t expiry_for(uint64_t period_us) const {
        uint64_t life = _lifetime_factor * period_us;
        if (life < _lifetime_min_us) life = _lifetime_min_us;
        return Clock::now_ns() + static_cast<int64_t>(life) * 1000;
    }

    // removes expired interests
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
            _bindings.erase(
                std::remove_if(_bindings.begin(), _bindings.end(),
                    [&](const Binding & b) { return now > b.expiry_ns; }),
                _bindings.end());
            resync_threads();
        }
    }

    std::function<void(Unit)> _respond;
    uint64_t _lifetime_min_us;
    unsigned _lifetime_factor;
    std::vector<Binding> _bindings;
    std::map<Unit, std::unique_ptr<Periodic_Thread>> _threads;
    std::mutex _mtx;
    std::atomic<bool> _running;
    std::thread _reaper;
};

#endif

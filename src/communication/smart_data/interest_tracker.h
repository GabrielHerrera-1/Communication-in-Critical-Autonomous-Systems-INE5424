#ifndef INTEREST_TRACKER_H
#define INTEREST_TRACKER_H

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "../../channel/vehicle_protocol.h"
#include "../../core/clock.h"
#include "../../core/observers/concurrent_observer.h"
#include "../../core/periodic_thread.h"
#include "../communicator.h"
#include "../message/message.h"
#include "smart_message.h"
#include "smart_helpers.h"
#include "unit.h"

// Broker de interesse na RSU (rastreamento passivo). E um Concurrent_Observer
// <Message> que observa o Communicator do gateway
class Interest_Tracker : public Concurrent_Observer<Message, Vehicle_Protocol::Port> {
public:
    using Base = Concurrent_Observer<Message, Vehicle_Protocol::Port>;
    using Comm = Communicator<Vehicle_Protocol>;
    using Port = Vehicle_Protocol::Port;
    using Address = Vehicle_Protocol::Address;

    // lease de presenca: veiculo nao visto (REQUEST_SYNC) por este tempo saiu
    static constexpr int64_t PRESENCE_LEASE_NS = 4'000'000'000LL; // 4s

    Interest_Tracker(Comm * comm, uint64_t repeat_us) : _comm(comm) {
        _comm->attach(this, _comm->broadcast_condition());
        // o broker se inscreve na presenca do PTP pelo proprio canal
        _comm->channel()->set_presence_handler([this](Address a) { on_presence(a); });
        _repeater = std::make_unique<Periodic_Thread>(repeat_us, [this] { broker_tick(); });
    }

    ~Interest_Tracker() override {
        _comm->channel()->set_presence_handler({});
        _comm->detach(this, _comm->broadcast_condition());
        _repeater.reset();
        drain_and_free(*this);
    }

    // Presenca: o SPTP master chama isto a cada REQUEST_SYNC (via Protocol).
    void on_presence(Address from) {
        std::lock_guard<std::mutex> lock(_mtx);
        _presence[mac_key(from)] = Clock::now_ns();
    }

    // push: registra interesses ouvidos (ignora respostas). Nao enfileira.
    void update(Port /*c*/, Message * m) override {
        if (m->size() >= sizeof(InterestMessage)) {
            const SmartHeader * h = reinterpret_cast<const SmartHeader *>(m->data());
            if (h->kind == SmartHeader::INTEREST) {
                const InterestMessage * im = reinterpret_cast<const InterestMessage *>(m->data());
                const Address addr = m->address();
                std::lock_guard<std::mutex> lock(_mtx);
                if (im->disinterest) {
                    // desinteresse explicito: remove so as entradas DESTE address
                    _interests.erase(
                        std::remove_if(_interests.begin(), _interests.end(),
                            [&](const Entry & e) { return e.unit == h->unit && e.address == addr; }),
                        _interests.end());
                } else {
                    // quem manda interesse esta presente; atualiza/insere o registro
                    _presence[mac_key(addr)] = Clock::now_ns();
                    bool found = false;
                    for (auto & e : _interests) {
                        if (e.unit == h->unit && e.address == addr) {
                            e.period_us = im->period_us;
                            found = true;
                            break;
                        }
                    }
                    if (!found) _interests.push_back({h->unit, addr, im->period_us});
                }
            }
        }
        delete m;
    }

private:
    struct Entry {
        Unit     unit;
        Address  address;
        uint64_t period_us;
    };

    // Tick do broker:
    // tira veiculos cujo lease de presenca expirou e seus interesses
    // reanuncia os interesses ativos
    // manda desinteresse para as Units que ficaram sem ninguem desde o tick anterior (por presenca ou por desinteresse explicito)
    void broker_tick() {
        const int64_t now = Clock::now_ns();
        std::vector<std::pair<Unit, uint64_t>> to_repeat;
        std::vector<Unit> to_stop;
        {
            std::lock_guard<std::mutex> lock(_mtx);

            // tira veiculos ausentes + seus interesses
            std::vector<uint64_t> dead;
            for (const auto & kv : _presence)
                if (now - kv.second > PRESENCE_LEASE_NS) dead.push_back(kv.first);
            for (uint64_t mac : dead) {
                _presence.erase(mac);
                _interests.erase(
                    std::remove_if(_interests.begin(), _interests.end(),
                        [&](const Entry & e) { return mac_key(e.address) == mac; }),
                    _interests.end());
            }

            // conjunto ativo atual: Unit -> periodo mais curto
            std::map<Unit, uint64_t> active;
            for (const auto & e : _interests) {
                auto it = active.find(e.unit);
                if (it == active.end() || e.period_us < it->second) active[e.unit] = e.period_us;
            }

            // Units que sumiram desde o tick passado -> mandar parar
            for (Unit u : _active_units)
                if (active.find(u) == active.end()) to_stop.push_back(u);

            for (const auto & kv : active) to_repeat.push_back(kv);

            _active_units.clear();
            for (const auto & kv : active) _active_units.insert(kv.first);
        }

        for (Unit u : to_stop) {
            // msg de desinteresse
            send_msg(u, 0, true);
            uint64_t n = _disinterests_sent.fetch_add(1, std::memory_order_relaxed) + 1;
            std::cout << "[Interest_Tracker] sem interessados -> desinteresse unit=0x"
                      << std::hex << static_cast<uint32_t>(u) << std::dec
                      << " (total=" << n << ")" << std::endl;
        }
        for (const auto & kv : to_repeat)
            send_msg(kv.first, kv.second, /*disinterest=*/false);
    }

    void send_msg(Unit unit, uint64_t period_us, bool disinterest) {
        InterestMessage im;
        im.header.kind = SmartHeader::INTEREST;
        im.header.unit = unit;
        im.period_us = period_us;
        im.disinterest = disinterest ? 1 : 0;
        TypedMessage<InterestMessage> msg(im);
        _comm->send(&msg);
    }

    Comm * _comm;
    mutable std::mutex _mtx;
    std::vector<Entry> _interests;
    std::map<uint64_t, int64_t> _presence;   // mac -> last_seen_ns
    std::set<Unit> _active_units;            // Units com interesse no tick anterior
    std::atomic<uint64_t> _disinterests_sent{0};
    std::unique_ptr<Periodic_Thread> _repeater;
};

#endif

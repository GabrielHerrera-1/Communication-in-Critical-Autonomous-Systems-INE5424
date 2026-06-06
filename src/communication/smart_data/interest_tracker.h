#ifndef INTEREST_TRACKER_H
#define INTEREST_TRACKER_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "../../channel/vehicle_protocol.h"
#include "../../core/observers/concurrent_observer.h"
#include "../../core/periodic_thread.h"
#include "../communicator.h"
#include "../message/message.h"
#include "smart_message.h"
#include "unit.h"

// Rastreamento passivo na RSU ("a RSU ouviu os interesses, ela pode repetir").
//
// Mesmo modelo do SmartData: e um Concurrent_Observer<Message> que OBSERVA um
// Communicator. O update() registra os interesses que passam e uma
// Periodic_Thread os reenvia em broadcast -- cobre late joiners e perda sem o
// subscriber precisar reenviar. Generico: rastreia qualquer Unit.
//
// Cada registro guarda o ENDERECO de quem demonstrou interesse (vetor, um por
// (Unit, Address)): assim um desinteresse de UM agente remove so o registro
// dele, sem derrubar os outros interessados na mesma Unit. O repeat reenvia um
// interesse por Unit, no periodo mais curto entre os interessados dela.
class Interest_Tracker : public Concurrent_Observer<Message, Vehicle_Protocol::Port> {
public:
    using Base = Concurrent_Observer<Message, Vehicle_Protocol::Port>;
    using Comm = Communicator<Vehicle_Protocol>;
    using Port = Vehicle_Protocol::Port;
    using Address = Vehicle_Protocol::Address;

    Interest_Tracker(Comm * comm, uint64_t repeat_us) : _comm(comm) {
        _comm->attach(this, _comm->broadcast_condition());
        _repeater = std::make_unique<Periodic_Thread>(repeat_us, [this] { repeat_all(); });
    }

    ~Interest_Tracker() override {
        _comm->detach(this, _comm->broadcast_condition());
        _repeater.reset();
        drain_and_free();
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
                    // remove so as entradas DESTE address para a unit
                    _interests.erase(
                        std::remove_if(_interests.begin(), _interests.end(),
                            [&](const Entry & e) { return e.unit == h->unit && e.address == addr; }),
                        _interests.end());
                } else {
                    // atualiza o registro (unit, addr) se ja existe; senao adiciona
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

    void repeat_all() {
        // um interesse por Unit, no periodo mais curto entre os interessados dela
        std::map<Unit, uint64_t> shortest;
        {
            std::lock_guard<std::mutex> lock(_mtx);
            for (const auto & e : _interests) {
                auto it = shortest.find(e.unit);
                if (it == shortest.end() || e.period_us < it->second)
                    shortest[e.unit] = e.period_us;
            }
        }
        for (const auto & kv : shortest) {
            InterestMessage im;
            im.header.kind = SmartHeader::INTEREST;
            im.header.unit = kv.first;
            im.period_us = kv.second;
            im.disinterest = 0;
            TypedMessage<InterestMessage> msg(im);
            _comm->send(&msg);
        }
    }

    void drain_and_free() {
        Message * m;
        while ((m = Base::updated(0)) != nullptr) delete m;
    }

    Comm * _comm;
    std::mutex _mtx;
    std::vector<Entry> _interests;
    std::unique_ptr<Periodic_Thread> _repeater;
};

#endif

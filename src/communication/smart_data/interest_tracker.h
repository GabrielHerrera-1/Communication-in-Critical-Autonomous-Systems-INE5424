#ifndef INTEREST_TRACKER_H
#define INTEREST_TRACKER_H

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
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
// Communicator. O update() registra os interesses que passam (por Unit) e uma
// Periodic_Thread os reenvia em broadcast -- cobre late joiners e perda sem o
// subscriber precisar reenviar. Generico: rastreia qualquer Unit.
class Interest_Tracker : public Concurrent_Observer<Message, Vehicle_Protocol::Port> {
public:
    using Base = Concurrent_Observer<Message, Vehicle_Protocol::Port>;
    using Comm = Communicator<Vehicle_Protocol>;
    using Port = Vehicle_Protocol::Port;

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
                std::lock_guard<std::mutex> lock(_mtx);
                if (im->disinterest) _interests.erase(h->unit); // subscriber saiu
                else                 _interests[h->unit] = im->period_us;
            }
        }
        delete m;
    }

private:
    void repeat_all() {
        std::vector<std::pair<Unit, uint64_t>> snapshot;
        {
            std::lock_guard<std::mutex> lock(_mtx);
            snapshot.assign(_interests.begin(), _interests.end());
        }
        for (const auto & kv : snapshot) {
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
    std::map<Unit, uint64_t> _interests;
    std::unique_ptr<Periodic_Thread> _repeater;
};

#endif

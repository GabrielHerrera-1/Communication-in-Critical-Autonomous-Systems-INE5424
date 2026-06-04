#ifndef INTEREST_TRACKER_H
#define INTEREST_TRACKER_H

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "../../channel/vehicle_protocol.h"
#include "../communicator.h"
#include "../message/message.h"
#include "../../core/periodic_thread.h"
#include "smart_message.h"
#include "unit.h"

// Rastreamento passivo de interesses na RSU ("a RSU ouviu os interesses, ela
// pode repetir os interesses").
//
// E um Communicator: o update() (push) registra cada Interesse que passa (por
// Unit) e os reenvia periodicamente em broadcast. Cobre late joiners e perda
// sem o subscriber precisar reenviar. Generico: rastreia qualquer Unit.
class Interest_Tracker : public Communicator<Vehicle_Protocol> {
public:
    using Base    = Communicator<Vehicle_Protocol>;
    using Address = Vehicle_Protocol::Address;
    using Port    = Vehicle_Protocol::Port;

    Interest_Tracker(Vehicle_Protocol * channel, Port port, uint64_t repeat_us)
        : Base(channel, port, /*subscribe_broadcast=*/true) {
        this->attach_channel();
        _repeater = std::make_unique<Periodic_Thread>(
            repeat_us, [this] { repeat_all(); });
    }

    ~Interest_Tracker() override {
        this->detach_channel();
        _repeater.reset();
    }

    // push: registra interesses ouvidos (ignora respostas).
    void update(typename Base::Condition /*c*/, typename Base::Buffer * buf) override {
        unsigned char payload[Vehicle_Protocol::MTU];
        Address from;
        int64_t ts = 0;
        uint8_t q = MessageHeader::QUADRANT_NONE;
        int size = this->_channel->receive(buf, &from, &ts, &q, payload, sizeof(payload));
        if (size < static_cast<int>(sizeof(SmartHeader))) return;

        const SmartHeader * h = reinterpret_cast<const SmartHeader *>(payload);
        if (h->kind != SmartHeader::INTEREST) return;
        if (size < static_cast<int>(sizeof(InterestMessage))) return;

        const InterestMessage * im = reinterpret_cast<const InterestMessage *>(payload);
        std::lock_guard<std::mutex> lock(_mtx);
        if (im->disinterest) {
            _interests.erase(h->unit);  // subscriber saiu: para de repetir
        } else {
            _interests[h->unit] = im->period_us;
        }
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
            this->send(&msg);
        }
    }

    std::mutex _mtx;
    std::map<Unit, uint64_t> _interests;
    std::unique_ptr<Periodic_Thread> _repeater;
};

#endif

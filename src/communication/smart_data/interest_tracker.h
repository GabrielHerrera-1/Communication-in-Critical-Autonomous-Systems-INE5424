#ifndef INTEREST_TRACKER_H
#define INTEREST_TRACKER_H

#include <atomic>
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

// Rastreamento passivo de interesses na RSU (anotacao: "a RSU ouviu os
// interesses, ela pode repetir os interesses").
//
// A RSU e uma estacao fixa que escuta passivamente todo o trafego. O tracker
// registra os interesses que passam (por Unit) e os REENVIA periodicamente em
// broadcast. Isso cobre dois casos sem exigir que o subscriber fique reenviando:
//   - um produtor que entra DEPOIS (late joiner) aprende o interesse vigente;
//   - perda do interesse original e recuperada pela repeticao.
//
// E generico (nao parametrizado por tipo): rastreia qualquer Unit. Diferente do
// SmartData, nao filtra por uma Unit especifica.
class Interest_Tracker {
public:
    using Comm = Communicator<Vehicle_Protocol>;

    Interest_Tracker(Comm * comm, uint64_t repeat_us)
        : _comm(comm), _running(true) {
        _repeater = std::make_unique<Periodic_Thread>(
            repeat_us, [this] { repeat_all(); });
    }

    ~Interest_Tracker() {
        _running.store(false, std::memory_order_release);
        _repeater.reset();
    }

    // Laco foreground: escuta interesses e os registra. Nao retorna.
    void serve() {
        Message raw;
        while (_running.load(std::memory_order_acquire)) {
            if (!_comm->receive(&raw)) continue;
            if (raw.size() < sizeof(SmartHeader)) continue;
            const SmartHeader * h = reinterpret_cast<const SmartHeader *>(raw.data());
            if (h->kind != SmartHeader::INTEREST) continue; // ignora respostas
            if (raw.size() < sizeof(InterestMessage)) continue;

            const InterestMessage * im =
                reinterpret_cast<const InterestMessage *>(raw.data());
            std::lock_guard<std::mutex> lock(_mtx);
            if (im->disinterest) {
                _interests.erase(h->unit);  // subscriber saiu: para de repetir
            } else {
                _interests[h->unit] = im->period_us;
            }
        }
    }

private:
    // Reenvia todos os interesses rastreados (roda na Periodic_Thread).
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

    Comm * _comm;
    std::atomic<bool> _running;
    std::mutex _mtx;
    std::map<Unit, uint64_t> _interests;
    std::unique_ptr<Periodic_Thread> _repeater;
};

#endif

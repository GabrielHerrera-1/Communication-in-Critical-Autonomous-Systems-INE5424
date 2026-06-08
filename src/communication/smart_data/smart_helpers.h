#ifndef SMART_HELPERS_H
#define SMART_HELPERS_H

#include <cstdint>

#include "../../channel/vehicle_protocol.h"
#include "../../core/observers/concurrent_observer.h"
#include "../message/message.h"

// helpers compartilhados entre SmartData e Interest_Tracker 

// chave de 64 bits a partir do MAC (6 bytes) -- identidade do veiculo
inline uint64_t mac_key(const Vehicle_Protocol::Address & a) {
    const uint8_t * mac = a.paddr().raw();
    uint64_t k = 0;
    for (int i = 0; i < 6; ++i) k = (k << 8) | mac[i];
    return k;
}

// drena e libera todas as Messages pendentes na fila do observer
inline void drain_and_free(Concurrent_Observer<Message, Vehicle_Protocol::Port> & obs) {
    Message * m;
    while ((m = obs.updated(0)) != nullptr) delete m;
}

#endif

#ifndef LOCAL_PROTOCOL_H
#define LOCAL_PROTOCOL_H

#include "../network/nic.h"
#include "../network/engine/shared_memory_engine.h"
#include "../channel/protocol.h"

class Local_Protocol : public Protocol<NIC<SharedMemoryEngine>> {
public:
    explicit Local_Protocol(NIC<SharedMemoryEngine> * nic)
        : Protocol<NIC<SharedMemoryEngine>>(nic) {}
};

#endif

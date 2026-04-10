#ifndef VEHICLE_PROTOCOL
#define VEHICLE_PROTOCOL

#include "../network/nic.h"
#include "../network/engine/raw_socket_engine.h"
#include "../network/engine/shared_memory_engine.h"
#include "../channel/protocol.h"

using Vehicle_Protocol = Protocol<NIC<SharedMemoryEngine>, NIC<RawSocketEngine>>;

#endif

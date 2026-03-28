#ifndef VEHICLE_PROTOCOL
#define VEHICLE_PROTOCOL

#include "../network/nic.h"
#include "../channel/protocol.h"

// talvez fazer com que a engine seja generica
class Vehicle_Protocol : public Protocol<NIC<RawSocketEngine>> {
public:
    
    Vehicle_Protocol(NIC<RawSocketEngine> * nic) : Protocol<NIC<RawSocketEngine>>(nic) {}
};

#endif
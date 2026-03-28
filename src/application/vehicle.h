#ifndef VEHICLE_H
#define VEHICLE_H

#include "../network/nic.h"
#include "../network/ethernet.h"
#include "../network/engine/raw_socket_engine.h"
#include "../channel/protocol.h"
#include "../communication/communicator.h"
#include "vehicle_protocol.h"
#include "components/component.h"
#include <vector>

class Vehicle {
public:
    typedef Vehicle_Protocol::Port Port;
    typedef Vehicle_Protocol::Address Address;

    Vehicle(Port port);
    ~Vehicle();

    void add_component(Component* component);

    void initialize();

    void run();

    void i_am_loop();

    Address you_are();

private:

    NIC<RawSocketEngine> _nic;
    Vehicle_Protocol _protocol;
    Address _addr;
    Communicator<Vehicle_Protocol> _communicator;

    std::vector<Component*> _components;

};

#endif
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
    Vehicle();
    ~Vehicle();

    // adiciona componente (vehicle assume ownership)
    void add_component(Component* component);
    // inicializa todos os componentes
    void initialize();
    // fork de cada componente em um processo separado
    void run();

private:
    // stack de rede (ordem de declaração importa pra inicialização)
    NIC<RawSocketEngine> _nic;
    Vehicle_Protocol _protocol;

    std::vector<Component*> _components;

    Vehicle_Protocol::Address generate_addres();

    Vehicle_Protocol::Port _port_counter = 0;

};

#endif
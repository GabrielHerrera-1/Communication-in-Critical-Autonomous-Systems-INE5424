#ifndef VEHICLE_H
#define VEHICLE_H

#include "../network/nic.h"
#include "../network/ethernet.h"
#include "../network/engine/raw_socket_engine.h"
#include "../channel/protocol.h"
#include "vehicle_protocol.h"
#include "components/component.h"
#include <vector>

class Vehicle {
public:

    typedef std::pair<Component*,Vehicle_Protocol::Port> Component_Port_Pair;

    Vehicle();
    ~Vehicle();

    // adiciona componente (vehicle assume ownership)
    void add_component(Component* component);
    // adicona component e gera porta explicitamente
    void add_component(Component* component, Vehicle_Protocol::Port port);
    // inicializa todos os componentes
    void initialize();
    // fork de cada componente em um processo separado
    void run();

private:

    std::vector<Component_Port_Pair> _components;

    Vehicle_Protocol::Port _port_counter = 0;

};

#endif
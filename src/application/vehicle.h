#ifndef VEHICLE_H
#define VEHICLE_H

#include "../network/nic.h"
#include "../network/ethernet.h"
#include "../network/engine/raw_socket_engine.h"
#include "../channel/protocol.h"
#include "gateway.h"
#include "vehicle_protocol.h"
#include "components/component.h"
#include <vector>
#include <sys/types.h>

class Vehicle {
public:

    typedef std::pair<Component*, Component::Port> Component_Port_Pair;

    Vehicle();
    ~Vehicle();

    // adiciona componente (vehicle assume ownership)
    void add_component(Component* component);
    // adicona component e gera porta explicitamente
    void add_component(Component* component, Component::Port port);
    // inicializa todos os componentes
    void initialize();
    // fork de cada componente em um processo separado
    void run();

private:
    int run_gateway_process();
    pid_t spawn_component_process(unsigned int index,
                                  const SharedMemoryEngine::Context & context);
    int run_component_process(unsigned int index,
                              const SharedMemoryEngine::Context & context);
    std::vector<uint16_t> component_ports() const;

    std::vector<Component_Port_Pair> _components;
    Gateway _gateway;
};

#endif

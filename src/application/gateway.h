#ifndef GATEWAY_H
#define GATEWAY_H

#include "../network/nic.h"
#include "../network/engine/raw_socket_engine.h"
#include "../network/engine/shared_memory_engine.h"
#include "vehicle_protocol.h"
#include "local_protocol.h"
#include "components/component.h"
#include <vector>
#include <map>
#include <csignal>

class Gateway {
public:
    Gateway(const SharedMemoryEngine::Config & local_config,
            const std::vector<Component::Port> & component_ports);

    int run();

private:
    static void handle_stop_signal(int);
    void install_signal_handlers();

    void attach_inboxes();
    void process_local_messages();
    void process_network_messages();
    void route_local_message(Local_Protocol::Buffer * buf);
    void route_network_message(Vehicle_Protocol::Buffer * buf);
    void forward_to_local(const Local_Protocol::Address & from,
                          Component::Port destination_port,
                          const void * payload,
                          unsigned int size,
                          const Local_Protocol::Address * exclude_source);
    void forward_to_network(Component::Port source_port,
                            Component::Port destination_port,
                            const void * payload,
                            unsigned int size);
    std::vector<Local_Protocol::Address> local_targets(Component::Port port) const;

private:
    std::vector<Component::Port> _component_ports;
    NIC<SharedMemoryEngine> _local_nic;
    Local_Protocol _local_protocol;
    Local_Protocol::Observer _local_inbox;
    NIC<RawSocketEngine> _network_nic;
    Vehicle_Protocol _network_protocol;
    Vehicle_Protocol::Observer _network_inbox;
    std::map<Component::Port, std::vector<Local_Protocol::Address>> _routes;

    static volatile sig_atomic_t _stop_requested;
};

#endif

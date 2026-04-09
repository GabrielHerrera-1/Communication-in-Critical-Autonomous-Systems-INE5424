#include "gateway.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/select.h>
#include <unistd.h>

volatile sig_atomic_t Gateway::_stop_requested = 0;

Gateway::Gateway(const SharedMemoryEngine::Config & local_config,
                 const std::vector<Component::Port> & component_ports)
    : _component_ports(component_ports),
      _local_nic(local_config),
      _local_protocol(&_local_nic),
      _local_inbox(),
      _network_nic(),
      _network_protocol(&_network_nic),
      _network_inbox(),
      _routes() {
    attach_inboxes();

    for (Component::Port port : _component_ports) {
        _routes[port].push_back(
            Local_Protocol::Address(SharedMemoryEngine::component_address(port), port)
        );
    }
}

int Gateway::run() {
    install_signal_handlers();
    _local_nic.nonblocking(true);
    _network_nic.nonblocking(true);

    const int local_fd = _local_nic.fd();
    const int network_fd = _network_nic.fd();
    if (local_fd < 0 || network_fd < 0) {
        std::fprintf(stderr, "[Gateway] meios de comunicacao indisponiveis\n");
        return 1;
    }

    while (!_stop_requested) {
        process_local_messages();
        process_network_messages();

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(local_fd, &readfds);
        FD_SET(network_fd, &readfds);

        int max_fd = (local_fd > network_fd) ? local_fd : network_fd;
        int rc = select(max_fd + 1, &readfds, nullptr, nullptr, nullptr);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("[Gateway] select");
            return 1;
        }

        if (FD_ISSET(local_fd, &readfds)) {
            while (_local_protocol.dispatch_once() > 0) {
            }
        }

        if (FD_ISSET(network_fd, &readfds)) {
            while (_network_protocol.dispatch_once() > 0) {
            }
        }
    }

    return 0;
}

void Gateway::handle_stop_signal(int) {
    _stop_requested = 1;
}

void Gateway::install_signal_handlers() {
    struct sigaction stop_sa;
    std::memset(&stop_sa, 0, sizeof(stop_sa));
    stop_sa.sa_handler = handle_stop_signal;
    sigemptyset(&stop_sa.sa_mask);
    sigaction(SIGTERM, &stop_sa, nullptr);
    sigaction(SIGINT, &stop_sa, nullptr);
}

void Gateway::attach_inboxes() {
    for (Component::Port port : _component_ports) {
        _local_protocol.attach(
            &_local_inbox,
            Local_Protocol::Address(SharedMemoryEngine::gateway_address(), port)
        );
        _network_protocol.attach(
            &_network_inbox,
            _network_protocol.create_address(port)
        );
    }
}

void Gateway::process_local_messages() {
    Local_Protocol::Buffer * buf = nullptr;
    while (_local_inbox.try_updated(&buf)) {
        route_local_message(buf);
    }
}

void Gateway::process_network_messages() {
    Vehicle_Protocol::Buffer * buf = nullptr;
    while (_network_inbox.try_updated(&buf)) {
        route_network_message(buf);
    }
}

void Gateway::route_local_message(Local_Protocol::Buffer * buf) {
    Message message;
    Local_Protocol::Address from;
    Local_Protocol::Address to;
    int size = _local_protocol.receive(buf, &from, &to, message.data(), message.size());
    if (size <= 0) {
        return;
    }

    message.size(size);
    message.origin(Message::Origin(from.paddr(), from.port()));

    forward_to_local(from, to.port(), message.data(), message.size(), &from);
    forward_to_network(from.port(), to.port(), message.data(), message.size());
}

void Gateway::route_network_message(Vehicle_Protocol::Buffer * buf) {
    Message message;
    Vehicle_Protocol::Address from;
    Vehicle_Protocol::Address to;
    int size = _network_protocol.receive(buf, &from, &to, message.data(), message.size());
    if (size <= 0) {
        return;
    }

    message.size(size);
    message.origin(Message::Origin(from.paddr(), from.port()));

    forward_to_local(
        Local_Protocol::Address(from.paddr(), from.port()),
        to.port(),
        message.data(),
        message.size(),
        nullptr
    );
}

void Gateway::forward_to_local(const Local_Protocol::Address & from,
                               Component::Port destination_port,
                               const void * payload,
                               unsigned int size,
                               const Local_Protocol::Address * exclude_source) {
    std::vector<Local_Protocol::Address> targets = local_targets(destination_port);

    for (const Local_Protocol::Address & target : targets) {
        if (exclude_source &&
            target.port() == exclude_source->port() &&
            target.paddr() == exclude_source->paddr()) {
            continue;
        }

        Local_Protocol::send(from, target, payload, size);
    }
}

void Gateway::forward_to_network(Component::Port source_port,
                                 Component::Port destination_port,
                                 const void * payload,
                                 unsigned int size) {
    Vehicle_Protocol::send(
        _network_protocol.create_address(source_port),
        Vehicle_Protocol::Address::broadcast(destination_port),
        payload,
        size
    );
}

std::vector<Local_Protocol::Address> Gateway::local_targets(Component::Port port) const {
    auto it = _routes.find(port);
    if (it == _routes.end()) {
        return {};
    }

    return it->second;
}

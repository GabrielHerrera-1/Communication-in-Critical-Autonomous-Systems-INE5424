#include "vehicle.h"
#include <sys/wait.h>
#include <cstdlib>
#include <iostream>

Vehicle::Vehicle(){}

Vehicle::~Vehicle() {
    for (auto c : _components)
        delete c.first;
}

void Vehicle::add_component(Component* component) {
    Component::Port port = _port_counter++;
    _components.push_back(Component_Port_Pair(component,port));
}

void Vehicle::add_component(Component* component, Component::Port port) {
    _components.push_back(Component_Port_Pair(component,port));
}

void Vehicle::initialize() {
    for (auto c : _components)
        c.first->initialize();
}

void Vehicle::run() {
    std::vector<pid_t> pids;
    bool failed = false;

    // fork de cada componente para processo separado
    for (auto c : _components) {
        pid_t pid = fork();
        if (pid == 0) {
            NIC<RawSocketEngine> nic;
            Vehicle_Protocol protocol(&nic);
            Channel_Endpoint<Vehicle_Protocol> endpoint(&protocol, protocol.create_address(c.second));
            c.first->set_endpoint(&endpoint);
            c.first->set_port(c.second);
            c.first->run();
            _exit(0);
        } else if (pid > 0) {
            pids.push_back(pid);
        } else {
            std::cerr << "[Vehicle] fork falhou para " << c.first->id() << std::endl;
        }
    }

    // esperar todos os componentes filhos terminarem
    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            failed = true;
            std::cerr << "[Vehicle] componente terminou com falha." << std::endl;
        }
    }

    if (failed) {
        std::cerr << "[Vehicle] encerrado com falha." << std::endl;
        std::exit(1);
    }

    std::cout << "[Vehicle] encerrado." << std::endl;
}

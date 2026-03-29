#include "vehicle.h"
#include <sys/wait.h>
#include <iostream>

Vehicle::Vehicle(){}

Vehicle::~Vehicle() {
    for (auto c : _components)
        delete c.first;
}

void Vehicle::add_component(Component* component) {
    Vehicle_Protocol::Port port = _port_counter++;
    _components.push_back(Component_Port_Pair(component,port));
    /*
    Communicator<Vehicle_Protocol>* communicator = new Communicator<Vehicle_Protocol>(&_protocol,_protocol.create_address(port));
    component->set_comunicator(communicator);
    component->set_port(port);
    */
}

void Vehicle::add_component(Component* component, Vehicle_Protocol::Port port) {
    _components.push_back(Component_Port_Pair(component,port));
}

void Vehicle::initialize() {
    for (auto c : _components)
        c.first->initialize();
}

void Vehicle::run() {
    std::vector<pid_t> pids;

    // fork de cada componente para processo separado
    for (auto c : _components) {
        pid_t pid = fork();
        if (pid == 0) {
            NIC<RawSocketEngine> nic;
            Vehicle_Protocol protocol(&nic);
            Communicator<Vehicle_Protocol>* communicator = new Communicator<Vehicle_Protocol>(&protocol,protocol.create_address(c.second));
            c.first->set_comunicator(communicator);
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
    }

    std::cout << "[Vehicle] encerrado." << std::endl;
}

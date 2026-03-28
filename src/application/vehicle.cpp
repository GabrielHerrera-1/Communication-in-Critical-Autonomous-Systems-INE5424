#include "vehicle.h"
#include <sys/wait.h>
#include <iostream>

Vehicle::Vehicle()
    : _nic()
    , _protocol(&_nic)
    {}

Vehicle::~Vehicle() {
    for (auto c : _components)
        delete c;
}

void Vehicle::add_component(Component* component) {
    _components.push_back(component);
}

void Vehicle::initialize() {
    for (auto c : _components)
        c->initialize();
}

void Vehicle::run() {
    std::vector<pid_t> pids;

    // fork de cada componente para processo separado
    for (auto c : _components) {
        pid_t pid = fork();
        if (pid == 0) {
            c->run();
            _exit(0);
        } else if (pid > 0) {
            pids.push_back(pid);
        } else {
            std::cerr << "[Vehicle] fork falhou para " << c->id() << std::endl;
        }
    }

    // esperar todos os componentes filhos terminarem
    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);
    }

    std::cout << "[Vehicle] encerrado." << std::endl;
}
#include "vehicle.h"
#include "gateway.h"
#include "local_protocol.h"
#include "../network/engine/shared_memory_engine.h"
#include "../network/nic.h"
#include "../communication/channel_endpoint.h"
#include <sys/wait.h>
#include <signal.h>
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
    std::vector<Component::Port> component_ports;
    bool failed = false;

    for (auto c : _components) {
        component_ports.push_back(c.second);
    }

    SharedMemoryEngine::Context shared_memory =
        SharedMemoryEngine::create(component_ports);
    if (shared_memory.shmid < 0 || shared_memory.semid < 0) {
        std::cerr << "[Vehicle] falha ao criar shared memory do gateway." << std::endl;
        std::exit(1);
    }

    pid_t gateway_pid = fork();
    if (gateway_pid == 0) {
        SharedMemoryEngine::Config local_config = {
            shared_memory,
            SharedMemoryEngine::ROLE_GATEWAY,
            0
        };
        Gateway gateway(local_config, component_ports);
        _exit(gateway.run());
    } else if (gateway_pid < 0) {
        std::cerr << "[Vehicle] fork falhou para o gateway." << std::endl;
        SharedMemoryEngine::destroy(shared_memory);
        std::exit(1);
    }

    // fork de cada componente para processo separado
    for (auto c : _components) {
        pid_t pid = fork();
        if (pid == 0) {
            SharedMemoryEngine::Config local_config = {
                shared_memory,
                SharedMemoryEngine::ROLE_COMPONENT,
                c.second
            };
            NIC<SharedMemoryEngine> local_nic(local_config);
            Local_Protocol local_protocol(&local_nic);
            Channel_Endpoint<Local_Protocol> endpoint(
                &local_protocol,
                Local_Protocol::Address(SharedMemoryEngine::component_address(c.second), c.second)
            );
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
        kill(gateway_pid, SIGTERM);
        waitpid(gateway_pid, nullptr, 0);
        SharedMemoryEngine::destroy(shared_memory);
        std::exit(1);
    }

    kill(gateway_pid, SIGTERM);
    waitpid(gateway_pid, nullptr, 0);
    SharedMemoryEngine::destroy(shared_memory);

    std::cout << "[Vehicle] encerrado." << std::endl;
}

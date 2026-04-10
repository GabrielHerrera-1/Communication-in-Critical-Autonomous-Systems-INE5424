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
    Component::Port port = component->logical_port();
    _components.push_back(Component_Port_Pair(component,port));
}

void Vehicle::add_component(Component* component, Component::Port port) {
    _components.push_back(Component_Port_Pair(component,port));
}

void Vehicle::initialize() {
    _gateway.initialize();
    for (auto c : _components)
        c.first->initialize();
}

void Vehicle::run() {
    std::vector<pid_t> pids;
    bool failed = false;
    std::vector<uint16_t> ports;
    ports.reserve(_components.size());

    for (const auto & c : _components) {
        ports.push_back(c.second);
    }

    SharedMemoryEngine::Context context =
        _gateway.create_context(ports.data(), static_cast<unsigned int>(ports.size()));

    if (context.shmid < 0 || context.semid < 0) {
        std::cerr << "[Vehicle] nao foi possivel criar a infraestrutura de SHM." << std::endl;
        std::exit(1);
    }

    _gateway.set_context(context);

    // fork de cada componente para processo separado
    for (unsigned int i = 0; i < _components.size(); ++i) {
        auto c = _components[i];
        pid_t pid = fork();
        if (pid == 0) {
            SharedMemoryEngine::Configuration config = {};
            config.context = context;
            config.slot = static_cast<uint16_t>(i + 1);
            config.port = c.second;
            SharedMemoryEngine::configure(config);

            Vehicle_Protocol protocol;
            Communicator<Vehicle_Protocol> communicator(&protocol, protocol.create_address(c.second));
            c.first->set_communicator(&communicator);
            c.first->set_port(c.second);
            c.first->run();
            _exit(0);
        } else if (pid > 0) {
            pids.push_back(pid);
        } else {
            std::cerr << "[Vehicle] fork falhou para " << c.first->id() << std::endl;
        }
    }

    _gateway.run();

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
        _gateway.stop();
        SharedMemoryEngine::destroy(context);
        std::exit(1);
    }

    _gateway.stop();
    SharedMemoryEngine::destroy(context);
    std::cout << "[Vehicle] encerrado." << std::endl;
}

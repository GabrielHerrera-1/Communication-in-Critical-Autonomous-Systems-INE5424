#include "vehicle.h"
#include "../core/rt_priority.h"
#include <csignal>
#include <memory>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <iostream>

Vehicle::Vehicle(bool is_master) : _is_master(is_master) {}

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
    _gateway.set_master(_is_master);
    _gateway.initialize();
    for (auto c : _components)
        c.first->initialize();
}

std::vector<uint16_t> Vehicle::component_ports() const {
    std::vector<uint16_t> ports;
    ports.reserve(_components.size());

    for (const auto & c : _components) {
        ports.push_back(c.second);
    }

    return ports;
}

int Vehicle::run_component_process(unsigned int index,
                                   const SharedMemoryEngine::Context & context) {
    auto c = _components[index];

    SharedMemoryEngine::Configuration config = {};
    config.context = context;
    config.slot = static_cast<uint16_t>(index + 1);
    config.port = c.second;
    SharedMemoryEngine::configure(config);

    Vehicle_Protocol protocol;
    c.first->set_channel(&protocol);
    c.first->set_port(c.second);

    // Componentes SmartData criam seu proprio observer; o Vehicle so injeta o
    // canal. Componentes legados recebem um Communicator bruto.
    std::unique_ptr<Communicator<Vehicle_Protocol>> communicator;
    if (c.first->wants_raw_communicator()) {
        communicator = std::make_unique<Communicator<Vehicle_Protocol>>(
            &protocol,
            protocol.create_address(c.second),
            c.first->subscribe_logical_broadcast());
        c.first->set_communicator(communicator.get());
    }

    c.first->run();
    return 0;
}

pid_t Vehicle::spawn_component_process(unsigned int index,
                                       const SharedMemoryEngine::Context & context) {
    pid_t pid = fork();
    if (pid == 0) {
        _exit(run_component_process(index, context));
    }
    return pid;
}

int Vehicle::run_gateway_process() {
    RT_Priority::set_main_thread_priority("gateway-main");

    std::vector<pid_t> pids;
    bool failed = false;
    bool spawn_failed = false;
    const std::vector<uint16_t> ports = component_ports();

    SharedMemoryEngine::Context context =
        _gateway.create_context(ports.data(), static_cast<unsigned int>(ports.size()));

    if (context.shmid < 0 || context.semid < 0) {
        std::cerr << "[Vehicle] nao foi possivel criar a infraestrutura de SHM." << std::endl;
        return 1;
    }

    _gateway.set_context(context);

    for (unsigned int i = 0; i < _components.size(); ++i) {
        pid_t pid = spawn_component_process(i, context);
        if (pid > 0) {
            pids.push_back(pid);
        } else {
            std::cerr << "[Vehicle] fork falhou para " << _components[i].first->id() << std::endl;
            spawn_failed = true;
            break;
        }
    }

    auto cleanup_children = [&]() {
        for (pid_t pid : pids) {
            kill(pid, SIGTERM);
        }
        for (pid_t pid : pids) {
            int status;
            waitpid(pid, &status, 0);
        }
    };

    if (spawn_failed) {
        cleanup_children();
        SharedMemoryEngine::destroy(context);
        return 1;
    }

    _gateway.run();

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
        return 1;
    }

    _gateway.stop();
    SharedMemoryEngine::destroy(context);
    return 0;
}

void Vehicle::run() {
    pid_t gateway_pid = fork();
    if (gateway_pid == 0) {
        _exit(run_gateway_process());
    }

    if (gateway_pid < 0) {
        std::cerr << "[Vehicle] nao foi possivel criar o processo gateway." << std::endl;
        std::exit(1);
    }

    int status = 0;
    waitpid(gateway_pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "[Vehicle] encerrado com falha." << std::endl;
        std::exit(1);
    }

    std::cout << "[Vehicle] encerrado." << std::endl;
}

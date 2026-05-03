#include "vehicle.h"
#include "../core/rt_priority.h"
#include <csignal>
#include <unistd.h>
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
    setenv("IS_RSU","false",1);
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

    // Protocol/Communicator precisam ser construidos ANTES de configurar
    // SCHED_DEADLINE: a construcao dispara pthread_create das threads de
    // recepcao (SHM e raw socket), e o kernel rejeita pthread_create quando
    // a thread mae esta em SCHED_DEADLINE sem SCHED_FLAG_RESET_ON_FORK.
    Vehicle_Protocol protocol;
    Communicator<Vehicle_Protocol> communicator(
        &protocol,
        protocol.create_address(c.second),
        c.first->subscribe_logical_broadcast()
    );
    c.first->set_communicator(&communicator);
    c.first->set_port(c.second);

    // escolhe a politica de scheduling conforme o perfil declarado pelo
    // componente: SCHED_DEADLINE para componentes do veiculo (sensores/
    // atuadores reais, periodo 100ms, runtime 5ms) e SCHED_FIFO para
    // componentes de teste que ainda dependem de sleep_for proprio.
    const auto profile = c.first->rt_profile();
    if (profile.policy == Component::RT_Profile::Policy::DEADLINE) {
        if (!RT_Priority::set_current_thread_deadline(profile.deadline,
                                                      c.first->id().c_str())) {
            // fallback para FIFO se o kernel/admission control rejeitar:
            // melhor rodar em best-effort do que o componente morrer no boot
            RT_Priority::set_current_thread_fifo(profile.fifo_priority,
                                                 c.first->id().c_str());
        }
    } else {
        RT_Priority::set_current_thread_fifo(profile.fifo_priority,
                                             c.first->id().c_str());
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

#include "rsu.h"
#include "../core/rt_priority.h"
#include "../network/engine/shared_memory_engine.h"

#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

RSU::RSU() {}
RSU::~RSU() {}

void RSU::initialize() {
    _gateway.set_master(true);
    _gateway.initialize();
}

int RSU::run_gateway_process() {
    RT_Priority::set_main_thread_priority("rsu-gateway-main");

    // SHM com 0 componentes: so o slot do gateway fica registrado. A infra
    // ainda e necessaria porque o Protocol sempre instancia um SHM NIC
    // (mesmo sem trafego interno, e mais simples que ter duas variantes).
    SharedMemoryEngine::Context context = _gateway.create_context(nullptr, 0);
    if (context.shmid < 0 || context.semid < 0) {
        std::cerr << "[RSU] nao foi possivel criar SHM." << std::endl;
        return 1;
    }
    
    _gateway.set_context(context);

    _gateway.run();

    std::cout << "[RSU] antena/master PTP rodando. aguardando slaves." << std::endl;

    // Mantem o processo vivo para que as threads de fundo do Protocol
    // (SHM recv, raw socket recv, SPTP) continuem atendendo slaves.
    while (true) {
        pause();
    }

    _gateway.stop();
    SharedMemoryEngine::destroy(context);
    return 0;
}

void RSU::run() {
    pid_t pid = fork();
    if (pid == 0) {
        _exit(run_gateway_process());
    }
    if (pid < 0) {
        std::cerr << "[RSU] fork do gateway falhou." << std::endl;
        std::exit(1);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "[RSU] encerrado com falha." << std::endl;
        std::exit(1);
    }

    std::cout << "[RSU] encerrado." << std::endl;
}

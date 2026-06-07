#include "rsu.h"
#include "../core/rt_priority.h"
#include "../network/engine/shared_memory_engine.h"
#include "../communication/communicator.h"
#include "../communication/smart_data/interest_tracker.h"
#include "component_ports.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace {
// Le so2.rsu_repeat_us=<us> do /proc/cmdline (a VM nao herda env do host; a
// configuracao chega pela linha de comando do kernel, como so2.vm_id). 0 =
// rastreamento passivo desligado.
uint64_t read_rsu_repeat_us() {
    FILE * f = std::fopen("/proc/cmdline", "r");
    if (!f) return 0;
    char line[4096];
    if (!std::fgets(line, sizeof(line), f)) { std::fclose(f); return 0; }
    std::fclose(f);
    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        unsigned long long v = 0;
        if (std::sscanf(tok, "so2.rsu_repeat_us=%llu", &v) == 1)
            return static_cast<uint64_t>(v);
    }
    return 0;
}
} // namespace

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

    std::cout << "[RSU] master SPTP pronto. cenario validado." << std::endl;

    // Etapa 5: a RSU E o broker do quadrante -- nó fixo, PTP master, sempre
    // presente. Logo o broker fica SEMPRE ativo: rastreia interesse + presenca,
    // reanuncia e manda parar. so2.rsu_repeat_us so ajusta o periodo de reanuncio.
    if (!_gateway.protocol()) {
        std::cerr << "[RSU] protocolo ausente; broker nao pode subir." << std::endl;
        return 1;
    }

    uint64_t repeat_us = read_rsu_repeat_us();
    if (repeat_us == 0) repeat_us = 1'500'000; // padrao: reanuncia a cada 1.5s
    std::cout << "[RSU] broker de interesse ativo (repeat_us=" << repeat_us << ")" << std::endl;

    Communicator<Vehicle_Protocol> comm(
        _gateway.protocol(),
        _gateway.protocol()->create_address(Component_Ports::GATEWAY),
        true);
    // O broker se liga sozinho a presenca do PTP (pelo proprio canal, dentro da
    // lib). A aplicacao so cria o broker -- nao cabla nada no Protocol.
    Interest_Tracker tracker(&comm, repeat_us);

    // Mantem o processo vivo: as threads de fundo do Protocol (SHM/raw recv,
    // SPTP) e o broker seguem atendendo os slaves do quadrante.
    while (true) { pause(); }
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

}

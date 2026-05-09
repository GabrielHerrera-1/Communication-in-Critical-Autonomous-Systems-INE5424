#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"
#include "../src/core/clock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>
#include <unistd.h>

// Objetivo: medir empiricamente em quanto tempo o relogio do slave
// drifta em relacao ao master APOS uma unica sincronizacao.
// Com esse grafico (offset vs tempo) escolhemos MAX_SILENCE_S como
// metade do tempo em que o drift excede o erro tolerado (principio
// da "meia-vida": atualizar o relogio antes que ele perca metade do
// tempo de validade).
//
// Setup:
//   - 2 VMs: VM1 = master, VM2 = slave.
//   - SO2_SPTP_MAX_SILENCE_S = 3600 antes do fork do gateway,
//     logo o slave so realiza a SYNC inicial (disparada em start())
//     e nao pede ressync durante o teste. Isso isola o drift puro
//     do quartz das VMs.
//   - Master envia N mensagens espacadas por SEND_INTERVAL_MS.
//   - Slave descarta as primeiras WARMUP mensagens (enquanto a
//     primeira SYNC ainda esta se propagando) e toma a proxima
//     como baseline. Pra cada amostra seguinte imprime:
//        t_rel_s, raw_offset_us, drift_us = raw_offset - baseline
//
// raw_offset = (relogio_slave - relogio_master) + delay_rede.
// Como delay_rede e ~constante, drift = raw_offset(t) - raw_offset(0)
// isola a componente de relogio.

namespace {

static const int VM_COUNT         = 2;
static const int MASTER_VM_ID     = 1;
static const int MESSAGE_COUNT    = 120;
static const int SEND_INTERVAL_MS = 500;   // 60s de observacao
static const int WARMUP_DISCARD   = 3;     // espera sync inicial estabilizar
static const int STARTUP_SLEEP_S  = 4;     // garante sync inicial antes do 1o envio

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) { std::exit(1); }
    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(tok, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) std::exit(1);
            return vm_id;
        }
    }
    std::exit(1);
}

class Drift_Master : public Component {
public:
    Drift_Master() : Component("drift-master") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) std::exit(1);

        sleep(STARTUP_SLEEP_S);

        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            const char payload[] = "tick";
            Message m(payload, sizeof(payload));
            if (!_communicator->send(&m)) {
                std::cerr << "[sptp-drift][master] falha ao enviar " << i << std::endl;
                std::exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SEND_INTERVAL_MS));
        }
        std::cout << "[sptp-drift][master] cenario validado." << std::endl;
    }

    bool subscribe_logical_broadcast() const override { return false; }
    Port logical_port() const override { return Component_Ports::TEST_SPTP_DRIFT; }
};

class Drift_Slave : public Component {
public:
    Drift_Slave() : Component("drift-slave") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) std::exit(1);

        int64_t baseline_offset = 0;
        int64_t baseline_local  = 0;
        bool baseline_set = false;

        int64_t max_abs_drift = 0;
        int64_t sum_drift = 0;
        int samples = 0;

        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            Message m;
            if (!_communicator->receive(&m)) {
                std::cerr << "[sptp-drift][slave] falha ao receber " << i << std::endl;
                std::exit(1);
            }

            int64_t local_now = Clock::now_ns();
            int64_t master_ts = m.timestamp();
            int64_t raw_offset = local_now - master_ts; // ns

            if (i < WARMUP_DISCARD) continue;

            if (!baseline_set) {
                baseline_offset = raw_offset;
                baseline_local  = local_now;
                baseline_set = true;
                std::cout << "[sptp-drift][slave] baseline: raw_offset="
                          << (raw_offset / 1000) << " us" << std::endl;
                continue;
            }

            int64_t drift_ns = raw_offset - baseline_offset;
            int64_t t_rel_ns = local_now - baseline_local;

            // imprime periodicamente (a cada ~2s) pra nao poluir o log
            if (samples % 4 == 0) {
                std::cout << "[sptp-drift][slave] t="
                          << (t_rel_ns / 1'000'000'000LL) << "s"
                          << " raw_offset=" << (raw_offset / 1000) << " us"
                          << " drift=" << (drift_ns / 1000) << " us"
                          << std::endl;
            }

            int64_t abs_drift = drift_ns < 0 ? -drift_ns : drift_ns;
            if (abs_drift > max_abs_drift) max_abs_drift = abs_drift;
            sum_drift += drift_ns;
            samples++;
        }

        int64_t avg_drift = samples > 0 ? sum_drift / samples : 0;
        std::cout << "[sptp-drift][slave] RESUMO samples=" << samples
                  << " drift_max_abs=" << (max_abs_drift / 1000) << " us"
                  << " drift_medio=" << (avg_drift / 1000) << " us"
                  << std::endl;
        std::cout << "[sptp-drift][slave] cenario validado." << std::endl;
    }

    Port logical_port() const override { return Component_Ports::TEST_SPTP_DRIFT; }
};

} // namespace

int main() {
    // desativa ressync automatica (1h de silencio = 1 unica SYNC no start)
    setenv("SO2_SPTP_MAX_SILENCE_S", "3600", 1);

    const int vm_id = detect_vm_id();


    Vehicle vehicle(vm_id == MASTER_VM_ID);
    if (vm_id == MASTER_VM_ID) {
        vehicle.add_component(new Drift_Master(), Component_Ports::TEST_SPTP_DRIFT);
    } else {
        vehicle.add_component(new Drift_Slave(), Component_Ports::TEST_SPTP_DRIFT);
    }
    vehicle.initialize();
    vehicle.run();
    return 0;
}

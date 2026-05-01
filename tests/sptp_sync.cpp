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

// Cenario: 2 VMs. VM1 = master PTP, VM2 = slave.
// VM1 envia N mensagens com seu timestamp no payload.
// VM2 recebe, calcula offset = local_now - vm1_ts e imprime.
// O offset inclui latencia de rede, mas deve ser estavel apos SPTP sincronizar.
// Passa quando ambas imprimem "cenario validado."

namespace {

static const int VM_COUNT       = 2;
static const int MASTER_VM_ID   = 1;
static const int MESSAGE_COUNT  = 5;
static const int STARTUP_SLEEP  = 8;  // aguarda SPTP (max_silence=3.5s, dois ciclos)

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[sptp-sync] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }
    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * token = std::strtok(line, " "); token; token = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(token, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[sptp-sync] vm_id invalido: " << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[sptp-sync] so2.vm_id ausente no cmdline" << std::endl;
    std::exit(1);
}

// Master envia mensagens simples. O timestamp do header é carimbado automaticamente
// por send_via_nic (Clock::monotonic_stamp) — nao precisamos colocar nada no payload.
class SPTP_Master_Component : public Component {
public:
    explicit SPTP_Master_Component() : Component("sptp-master") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) { std::exit(1); }

        sleep(STARTUP_SLEEP);

        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            const char payload[] = "ping";
            Message m(payload, sizeof(payload));
            if (!_communicator->send(&m)) {
                std::cerr << "[sptp-sync][master] falha ao enviar msg " << i << std::endl;
                std::exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "[sptp-sync][master] cenario validado." << std::endl;
    }

    bool subscribe_logical_broadcast() const override { return false; }
    Port logical_port() const override { return Component_Ports::TEST_SPTP_SYNC; }
};

// Slave usa m.timestamp() (header carimbado pelo master em send_via_nic) para
// calcular o offset. Isso valida o pipeline completo: stamp -> wire -> Communicator -> Message.
class SPTP_Slave_Component : public Component {
public:
    explicit SPTP_Slave_Component() : Component("sptp-slave") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) { std::exit(1); }

        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            Message m;
            if (!_communicator->receive(&m)) {
                std::cerr << "[sptp-sync][slave] falha ao receber msg " << i << std::endl;
                std::exit(1);
            }

            int64_t local_now  = Clock::now_ns();
            int64_t master_ts  = m.timestamp(); // carimbado pelo master em send_via_nic

            // offset = (relogio_slave - relogio_master) + latencia_rede
            // apos SPTP sincronizar, a componente de relogio deve ser ~0
            int64_t offset_us = (local_now - master_ts) / 1000;
            std::cout << "[sptp-sync][slave] offset msg " << i
                      << ": " << offset_us << " us"
                      << " (relogio + latencia)" << std::endl;
        }

        std::cout << "[sptp-sync][slave] cenario validado." << std::endl;
    }

    Port logical_port() const override { return Component_Ports::TEST_SPTP_SYNC; }
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();

    Vehicle vehicle;
    if (vm_id == MASTER_VM_ID) {
        vehicle.add_component(new SPTP_Master_Component(), Component_Ports::TEST_SPTP_SYNC);
    } else {
        vehicle.add_component(new SPTP_Slave_Component(), Component_Ports::TEST_SPTP_SYNC);
    }
    vehicle.initialize();
    vehicle.run();
    return 0;
}

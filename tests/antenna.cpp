#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace {

static const int VM_COUNT       = 2;
static const int RSU_VM_ID      = 1;
static const int OBSERVATION_S  = 12;  // MAX_SILENCE_S e ~3.5s, garante varias syncs

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

class Antenna_Observer : public Component {
public:
    Antenna_Observer() : Component("antenna-observer") {}
    void initialize() override {}

    void run() override {
        std::cout << "[antenna][slave] aguardando "
                  << OBSERVATION_S << "s por syncs da RSU..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(OBSERVATION_S));
        std::cout << "[antenna][slave] janela de observacao encerrada; "
                     "cenario validado." << std::endl;
    }

    bool subscribe_logical_broadcast() const override { return false; }
    Port logical_port() const override { return Component_Ports::TEST_ANTENNA; }
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();

    if (vm_id == RSU_VM_ID) {
        RSU rsu;
        rsu.initialize();
        // o main do RSU so retorna em erro; em sucesso o gateway fica vivo
        // ate o host encerrar a VM. Imprimimos a linha de sucesso cedo para
        // que o run_qemu_test.sh detecte o criterio e finalize o teste
        std::cout << "[antenna][rsu] master dedicado iniciado; "
                     "cenario validado." << std::endl;
        rsu.run();
    } else {
        Vehicle vehicle;
        vehicle.add_component(new Antenna_Observer(),
                              Component_Ports::TEST_ANTENNA);
        vehicle.initialize();
        vehicle.run();
    }
    return 0;
}

// Etapa 5 -- Refresh REATIVO do interesse (base da mobilidade implicita).
//
// Parte A: o consumidor reenvia o interesse SO apos um silencio de dados (sem
// GPS no cliente). Aqui nao ha produtor, entao o consumidor fica em silencio e
// reanuncia a cada INTEREST_REFRESH_US -- e isso que prova o keep-alive reativo
// que, na troca de quadrante, dispara o reenvio re-carimbado pela NIC. O teste
// de mobilidade multi-quadrante ponta-a-ponta vem com a Parte B (presenca na
// RSU). WITH_GPS=1 (irrelevante pro cliente agora; so mantem o cenario).

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/data_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

const int      VM_COUNT         = 2;
const int      RSU_VM_ID        = 1;
const uint64_t PERIOD_US        = 300'000;
const uint64_t NEEDED_REISSUES  = 2;
const int      MAX_WAIT_S       = 60;
const int      STARTUP_DELAY_S  = 5;

const char LABEL[] = "interest-quadrant";

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) { std::cerr << "[" << LABEL << "] sem /proc/cmdline" << std::endl; std::exit(1); }
    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[" << LABEL << "] falha lendo /proc/cmdline" << std::endl; std::exit(1);
    }
    std::fclose(cmdline);
    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(tok, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[" << LABEL << "] vm_id invalido: " << vm_id << std::endl; std::exit(1);
            }
            return vm_id;
        }
    }
    std::cerr << "[" << LABEL << "] so2.vm_id ausente" << std::endl; std::exit(1);
}

class Reactive_Subscriber : public Component {
public:
    explicit Reactive_Subscriber(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] subscriber reativo: reanuncia o interesse no silencio de dados" << std::endl;

        SmartData<Counter_Data> consumer(_communicator, PERIOD_US);

        uint64_t last = 0;
        for (int i = 0; i < MAX_WAIT_S && consumer.reissues() < NEEDED_REISSUES; ++i) {
            sleep(1);
            uint64_t r = consumer.reissues();
            if (r != last) {
                last = r;
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] silencio -> reenvio reativo do interesse (total=" << r << ")" << std::endl;
            }
        }

        const uint64_t r = consumer.reissues();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO reenvios_reativos=" << r << std::endl;

        if (r < NEEDED_REISSUES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL reissues=" << r << " < " << NEEDED_REISSUES << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();
    if (vm_id == RSU_VM_ID) { RSU rsu; rsu.initialize(); rsu.run(); return 0; }
    Vehicle vehicle(false);
    vehicle.add_component(new Reactive_Subscriber(vm_id), Component_Ports::TEST_INTEREST_SUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

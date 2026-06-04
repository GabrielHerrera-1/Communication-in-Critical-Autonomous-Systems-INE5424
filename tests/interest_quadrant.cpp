// Etapa 5 -- Interesse x quadrante (integra com a sincronizacao espacial da
// etapa 4). Anotacao: "se alguem vai ficar reenviando interesse, para de enviar
// na troca de quadrante".
//
// Cenario (2 VMs, WITH_GPS=1):
//   vm1: RSU (master) -- GPS congelado (estacao fixa).
//   vm2: subscriber MOVEL -- seu GPS troca de quadrante a cada ~3s. O refresh
//        do interesse e SUPRIMIDO a cada troca de quadrante; o teste verifica
//        que essas supressoes acontecem.
//
// Demonstra que o sistema Interesse/Resposta convive com o quadrante: o
// interessado deixa de reenviar durante a transicao (os bindings do quadrante
// antigo expiram e o interesse e reemitido naturalmente no novo quadrante).

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/transducer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

const int      VM_COUNT         = 2;
const int      RSU_VM_ID        = 1;
const uint64_t PERIOD_US        = 300'000;
const uint64_t NEEDED_SUPP      = 2;   // trocas de quadrante observadas
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

class Moving_Subscriber : public Component {
public:
    explicit Moving_Subscriber(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] subscriber movel: refresh suprime na troca de quadrante" << std::endl;

        SmartData<Counter_Transducer> consumer(_communicator, PERIOD_US);

        uint64_t last_reported = 0;
        for (int i = 0; i < MAX_WAIT_S && consumer.quadrant_suppressions() < NEEDED_SUPP; ++i) {
            sleep(1);
            uint64_t s = consumer.quadrant_suppressions();
            if (s != last_reported) {
                last_reported = s;
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] troca de quadrante -> interesse suprimido (total="
                          << s << ")" << std::endl;
            }
        }

        const uint64_t supp = consumer.quadrant_suppressions();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO supressoes_por_troca_de_quadrante=" << supp << std::endl;

        if (supp < NEEDED_SUPP) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL supressoes=" << supp << " < " << NEEDED_SUPP
                      << " (GPS carregado? VM movel?)" << std::endl;
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

    if (vm_id == RSU_VM_ID) {
        RSU rsu;
        rsu.initialize();
        rsu.run();
        return 0;
    }

    Vehicle vehicle(false);
    vehicle.add_component(new Moving_Subscriber(vm_id),
                          Component_Ports::TEST_INTEREST_SUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

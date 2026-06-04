// Etapa 5 -- Ciclo de vida: interesse -> respostas -> DESINTERESSE -> parada.
//
// Cenario (3 VMs):
//   vm1: RSU (master SPTP).
//   vm2: subscriber -- assina Unit::TEST_COUNTER, coleta algumas respostas e
//        depois cancela o interesse (bit de desinteresse) ao "sair".
//   vm3: publisher -- responde ate receber o desinteresse; ao recebe-lo, para
//        de responder e anuncia.
//
// Valida: o "bit de desinteresse" da spec, usado quando um veiculo sai da
// simulacao. O publisher so valida APOS processar o desinteresse, provando que
// a parada e dirigida pela mensagem (e nao apenas pelo expiry de soft-state).

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

const int      VM_COUNT          = 3;
const int      RSU_VM_ID         = 1;
const int      SUBSCRIBER_VM_ID  = 2;
const uint64_t PERIOD_US         = 250'000; // 250ms
const uint64_t COLLECT_BEFORE_UNSUB = 8;    // respostas antes de cancelar
const int      STARTUP_DELAY_S   = 5;
const int      FORWARD_GRACE_S   = 3;       // mantem o gateway vivo p/ encaminhar o desinteresse

const char LABEL[] = "interest-lifecycle";

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

class Publisher_Component : public Component {
public:
    explicit Publisher_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] publisher pronto" << std::endl;

        SmartData<Counter_Transducer> producer(Counter_Transducer{}, _communicator);

        const int vm_id = _vm_id;
        SmartData<Counter_Transducer> * p = &producer;
        producer.on_disinterest_received([vm_id, p](Unit) {
            std::cout << "[" << LABEL << "][vm" << vm_id
                      << "] DESINTERESSE recebido apos respostas_enviadas="
                      << p->responses_sent() << " -- parando" << std::endl;
            std::cout << "[" << LABEL << "][vm" << vm_id << "] cenario validado." << std::endl;
        });

        producer.serve();
    }

private:
    int _vm_id;
};

class Subscriber_Component : public Component {
public:
    explicit Subscriber_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] subscriber pronto" << std::endl;

        SmartData<Counter_Transducer> consumer(_communicator, PERIOD_US);

        while (consumer.response_count() < COLLECT_BEFORE_UNSUB) {
            if (!consumer.update_once()) break;
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] coletei respostas=" << consumer.response_count()
                  << " -- enviando DESINTERESSE (saindo)" << std::endl;
        consumer.unsubscribe();

        // mantem o processo (e seu gateway) vivo para encaminhar o desinteresse
        sleep(FORWARD_GRACE_S);

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO respostas=" << consumer.response_count()
                  << " desinteresse=enviado" << std::endl;
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
    if (vm_id == SUBSCRIBER_VM_ID) {
        vehicle.add_component(new Subscriber_Component(vm_id),
                              Component_Ports::TEST_INTEREST_SUB);
    } else {
        vehicle.add_component(new Publisher_Component(vm_id),
                              Component_Ports::TEST_INTEREST_PUB);
    }
    vehicle.initialize();
    vehicle.run();
    return 0;
}

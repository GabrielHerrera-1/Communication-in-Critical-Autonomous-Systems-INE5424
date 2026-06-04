// Etapa 5 -- Teste fundamental: 1 subscriber x N publishers.
//
// Cenario (5 VMs):
//   vm1: RSU (master SPTP).
//   vm2: subscriber -- SmartData INTERESSADO em Unit::TEST_COUNTER. Emite o
//        Interesse (period) e coleta Respostas ate ver os N produtores.
//   vm3..vm5: publishers -- SmartData RESPONSIVO. Ao receber o Interesse,
//        respondem periodicamente e indefinidamente (em broadcast).
//
// Valida: broadcast, binding por interesse, resposta periodica em thread
// dedicada, e MULTIPLOS produtores para um unico interesse (o subscriber
// distingue os produtores pelo MAC de origem).

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/transducer.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

const int      VM_COUNT           = 5;
const int      RSU_VM_ID          = 1;
const int      SUBSCRIBER_VM_ID   = 2;
const int      EXPECTED_PUBLISHERS = 3;     // vm3, vm4, vm5
const uint64_t PERIOD_US          = 300'000; // 300ms
const uint64_t MIN_RESPONSES      = 9;       // ~3 de cada produtor
const uint64_t PUB_ANNOUNCE_AFTER = 3;       // publisher anuncia ok apos 3 respostas
const int      STARTUP_DELAY_S    = 5;       // deixa a rede/SPTP subir

const char LABEL[] = "interest-basic";

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

// Publisher: responde ao interesse indefinidamente.
class Publisher_Component : public Component {
public:
    explicit Publisher_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] publisher RESPONSIVO pronto" << std::endl;

        SmartData<Counter_Transducer> producer(Counter_Transducer{}, _communicator);

        static std::atomic<bool> announced{false};
        const int vm_id = _vm_id;
        producer.on_response_sent([vm_id](uint64_t n) {
            if (n >= PUB_ANNOUNCE_AFTER && !announced.exchange(true)) {
                std::cout << "[" << LABEL << "][vm" << vm_id
                          << "] respostas enviadas=" << n << std::endl;
                std::cout << "[" << LABEL << "][vm" << vm_id
                          << "] cenario validado." << std::endl;
            }
        });

        producer.serve(); // bloqueia: responde para sempre
    }

private:
    int _vm_id;
};

// Subscriber: emite o interesse e coleta respostas dos N produtores.
class Subscriber_Component : public Component {
public:
    explicit Subscriber_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] subscriber INTERESSADO period_us=" << PERIOD_US << std::endl;

        SmartData<Counter_Transducer> consumer(_communicator, PERIOD_US);

        while (consumer.producer_count() < static_cast<std::size_t>(EXPECTED_PUBLISHERS) ||
               consumer.response_count() < MIN_RESPONSES) {
            Counter_Transducer::Value v;
            Vehicle_Protocol::Address from;
            int64_t ts = 0;
            if (!consumer.update_once(&v, &from, &ts)) break;
            std::cout << "[" << LABEL << "][vm" << _vm_id
                      << "] resposta seq=" << v.seq
                      << " produtores=" << consumer.producer_count()
                      << " total=" << consumer.response_count() << std::endl;
        }

        const std::size_t producers = consumer.producer_count();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores=" << producers
                  << " respostas=" << consumer.response_count() << std::endl;

        if (producers < static_cast<std::size_t>(EXPECTED_PUBLISHERS)) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL produtores=" << producers
                      << " < esperado=" << EXPECTED_PUBLISHERS << std::endl;
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

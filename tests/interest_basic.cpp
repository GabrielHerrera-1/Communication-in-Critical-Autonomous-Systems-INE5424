// Etapa 5 -- Teste fundamental: 1 subscriber x N publishers.
//
// vm1 RSU; vm2 subscriber (SmartData INTERESSADO em Unit::TEST_COUNTER);
// vm3..vm5 publishers RESPONSIVOS. Cada publisher e um componente que IMPLEMENTA
// IProducer (produz o dado); o SmartData responsivo so pega o valor via produce().
// Recepcao em modo push: o update() do SmartData interpreta a Resposta na hora.

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/iproducer.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/data_types.h"
#include "../src/core/clock.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

const int      VM_COUNT            = 5;
const int      RSU_VM_ID           = 1;
const int      SUBSCRIBER_VM_ID    = 2;
const std::size_t EXPECTED_PUBLISHERS = 3; // vm3, vm4, vm5
const uint64_t PERIOD_US           = 300'000;
const uint64_t MIN_RESPONSES       = 9;
const uint64_t PUB_ANNOUNCE_AFTER  = 1;
const int      STARTUP_DELAY_S     = 5;
const int64_t  DEADLINE_NS         = 90LL * 1000000000LL;

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

// Publisher: componente que PRODUZ o dado (implementa IProducer).
class Publisher_Component : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Publisher_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    bool wants_raw_communicator() const override { return false; }

    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] publisher RESPONSIVO pronto" << std::endl;

        SmartData<Counter_Data> producer(_channel, this, _port);

        static std::atomic<bool> announced{false};
        const int vm_id = _vm_id;
        producer.on_response_sent([vm_id](uint64_t n) {
            if (n >= PUB_ANNOUNCE_AFTER && !announced.exchange(true)) {
                std::cout << "[" << LABEL << "][vm" << vm_id
                          << "] respostas enviadas=" << n << std::endl;
                std::cout << "[" << LABEL << "][vm" << vm_id << "] cenario validado." << std::endl;
            }
        });

        while (true) pause(); // responde indefinidamente (update() trata interesses)
    }

private:
    int _vm_id;
    uint64_t _seq = 0;
};

// Subscriber: emite o interesse e coleta respostas dos N produtores.
class Subscriber_Component : public Component {
public:
    explicit Subscriber_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }
    bool wants_raw_communicator() const override { return false; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] subscriber INTERESSADO period_us=" << PERIOD_US << std::endl;

        SmartData<Counter_Data> consumer(_channel, PERIOD_US, _port);

        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while ((consumer.producer_count() < EXPECTED_PUBLISHERS ||
                consumer.response_count() < MIN_RESPONSES) &&
               Clock::now_ns() < deadline) {
            consumer.wait_for_responses(consumer.response_count() + 1, 2000);
        }

        const std::size_t producers = consumer.producer_count();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores=" << producers
                  << " respostas=" << consumer.response_count() << std::endl;

        if (producers < EXPECTED_PUBLISHERS) {
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

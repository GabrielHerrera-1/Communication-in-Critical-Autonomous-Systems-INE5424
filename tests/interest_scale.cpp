// Etapa 5 -- Escala: >=20 veiculos (1 subscriber x 20 publishers, 22 VMs).
// Memoria: rode com VMs pequenas (QEMU_MEM, ver makefile).

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

const int      VM_COUNT             = 22;
const int      RSU_VM_ID            = 1;
const int      SUBSCRIBER_VM_ID     = 2;
const std::size_t EXPECTED_PUBLISHERS = 20;
const uint64_t PERIOD_US            = 500'000;
const uint64_t MIN_RESPONSES        = 40;
const int      STARTUP_DELAY_S      = 8;
const int64_t  DEADLINE_NS          = 180LL * 1000000000LL;

const char LABEL[] = "interest-scale";

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

class Publisher_Component : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Publisher_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        SmartData<Counter_Data> producer(_communicator, this);
        static std::atomic<bool> announced{false};
        const int vm_id = _vm_id;
        producer.on_response_sent([vm_id](uint64_t n) {
            if (n >= 1 && !announced.exchange(true))
                std::cout << "[" << LABEL << "][vm" << vm_id << "] cenario validado." << std::endl;
        });
        while (true) pause();
    }

private:
    int _vm_id;
    uint64_t _seq = 0;
};

class Subscriber_Component : public Component {
public:
    explicit Subscriber_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] subscriber period_us=" << PERIOD_US << std::endl;

        SmartData<Counter_Data> consumer(_communicator, PERIOD_US);

        std::size_t last = 0;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while ((consumer.producer_count() < EXPECTED_PUBLISHERS ||
                consumer.response_count() < MIN_RESPONSES) &&
               Clock::now_ns() < deadline) {
            Message * m = consumer.receive_response(2000);
            if (m) delete m;
            std::size_t pc = consumer.producer_count();
            if (pc != last) {
                last = pc;
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] produtores distintos=" << pc << std::endl;
            }
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
    if (vm_id == RSU_VM_ID) { RSU rsu; rsu.initialize(); rsu.run(); return 0; }
    Vehicle vehicle(false);
    if (vm_id == SUBSCRIBER_VM_ID)
        vehicle.add_component(new Subscriber_Component(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Publisher_Component(vm_id), Component_Ports::TEST_INTEREST_PUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

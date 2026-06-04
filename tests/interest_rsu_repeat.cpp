// Etapa 5 -- Rastreamento passivo na RSU: a RSU repete os interesses ouvidos.
// vm1 RSU-tracker; vm2 subscriber SEM auto-refresh (manda e se cala); vm3
// publisher TARDIO. So a repeticao da RSU mantem o interesse vivo para o
// publisher tardio. Recepcao push.

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

const int      VM_COUNT         = 3;
const int      RSU_VM_ID        = 1;
const int      SUBSCRIBER_VM_ID = 2;
const uint64_t PERIOD_US        = 300'000;
const uint64_t MIN_RESPONSES    = 3;
const uint64_t PUB_ANNOUNCE_AFTER = 1;
const int      STARTUP_SUB_S    = 5;
const int      STARTUP_PUB_S    = 25;  // entra DEPOIS do subscriber se calar
const int64_t  DEADLINE_NS      = 90LL * 1000000000LL;

const char LABEL[] = "interest-rsu-repeat";

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

class Late_Publisher : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Late_Publisher(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    bool wants_raw_communicator() const override { return false; }
    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }

    void run() override {
        sleep(STARTUP_PUB_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] publisher TARDIO entrou (t~" << STARTUP_PUB_S << "s)" << std::endl;
        SmartData<Counter_Data> producer(_channel, this, _port);
        static std::atomic<bool> announced{false};
        const int vm_id = _vm_id;
        producer.on_response_sent([vm_id](uint64_t n) {
            if (n >= PUB_ANNOUNCE_AFTER && !announced.exchange(true)) {
                std::cout << "[" << LABEL << "][vm" << vm_id
                          << "] respondi ao interesse repetido pela RSU" << std::endl;
                std::cout << "[" << LABEL << "][vm" << vm_id << "] cenario validado." << std::endl;
            }
        });
        while (true) pause();
    }

private:
    int _vm_id;
    uint64_t _seq = 0;
};

class Silent_Subscriber : public Component {
public:
    explicit Silent_Subscriber(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }
    bool wants_raw_communicator() const override { return false; }

    void run() override {
        sleep(STARTUP_SUB_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] subscriber SEM auto-refresh: manda interesse e se cala" << std::endl;

        // auto_refresh=false: manda o interesse poucas vezes e para.
        SmartData<Counter_Data> consumer(_channel, PERIOD_US, _port, /*auto_refresh=*/false);

        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (consumer.response_count() < MIN_RESPONSES && Clock::now_ns() < deadline) {
            consumer.wait_for_responses(MIN_RESPONSES, 3000);
        }

        const std::size_t producers = consumer.producer_count();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores=" << producers
                  << " respostas=" << consumer.response_count()
                  << " (interesse mantido pela RSU)" << std::endl;

        if (producers < 1 || consumer.response_count() < MIN_RESPONSES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL sem respostas do publisher tardio" << std::endl;
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
        RSU rsu; rsu.initialize(); rsu.run(); return 0;
    }
    Vehicle vehicle(false);
    if (vm_id == SUBSCRIBER_VM_ID)
        vehicle.add_component(new Silent_Subscriber(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Late_Publisher(vm_id), Component_Ports::TEST_INTEREST_PUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

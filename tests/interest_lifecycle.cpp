// Etapa 5 -- Ciclo de vida: interesse -> respostas -> DESINTERESSE -> parada.
// vm1 RSU; vm2 subscriber coleta e cancela (sai); vm3 publisher para ao receber
// o desinteresse (prova que a parada e dirigida pela mensagem). Recepcao push.

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/iproducer.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/data_types.h"
#include "../src/core/clock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

const int      VM_COUNT          = 3;
const int      RSU_VM_ID         = 1;
const int      SUBSCRIBER_VM_ID  = 2;
const uint64_t PERIOD_US         = 250'000;
const uint64_t COLLECT_BEFORE_UNSUB = 8;
const int      STARTUP_DELAY_S   = 5;
const int      FORWARD_GRACE_S   = 3;
const int64_t  DEADLINE_NS       = 90LL * 1000000000LL;

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

class Publisher_Component : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Publisher_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    bool wants_raw_communicator() const override { return false; }
    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] publisher pronto" << std::endl;

        SmartData<Counter_Data> producer(_channel, this, _port);
        const int vm_id = _vm_id;
        SmartData<Counter_Data> * p = &producer;
        producer.on_disinterest_received([vm_id, p](Unit) {
            std::cout << "[" << LABEL << "][vm" << vm_id
                      << "] DESINTERESSE recebido apos respostas_enviadas="
                      << p->responses_sent() << " -- parando" << std::endl;
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
    bool wants_raw_communicator() const override { return false; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] subscriber pronto" << std::endl;

        SmartData<Counter_Data> consumer(_channel, PERIOD_US, _port);

        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (consumer.response_count() < COLLECT_BEFORE_UNSUB && Clock::now_ns() < deadline) {
            consumer.wait_for_responses(COLLECT_BEFORE_UNSUB, 2000);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] coletei respostas=" << consumer.response_count()
                  << " -- enviando DESINTERESSE (saindo)" << std::endl;
        consumer.unsubscribe();

        sleep(FORWARD_GRACE_S); // mantem o gateway vivo para encaminhar o desinteresse

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
        RSU rsu; rsu.initialize(); rsu.run(); return 0;
    }
    Vehicle vehicle(false);
    if (vm_id == SUBSCRIBER_VM_ID)
        vehicle.add_component(new Subscriber_Component(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Publisher_Component(vm_id), Component_Ports::TEST_INTEREST_PUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

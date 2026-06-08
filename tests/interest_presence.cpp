// etapa 5 (Parte B) -- "mandar parar" por PRESENCA (PTP)
//
// vm1 RSU broker, vm2 produtor, vm3 consumidor que abandona (sai sem mandar
// desinteresse, simulando crash/saida abrupta). como o consumidor nao envia
// desinteresse, o unico jeito de o produtor parar e a RSU detectar a saida por
// PRESENCA (ausencia de REQUEST_SYNC -> lease expira) e encaminhar o
// desinteresse. logo: se o produtor receber um desinteresse, foi a RSU

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

const int      VM_COUNT       = 3;
const int      RSU_VM_ID      = 1;
const int      PRODUCER_VM_ID = 2;
const int      CONSUMER_VM_ID = 3;
const uint64_t PERIOD_US      = 300'000;
const int      NEED_RESPONSES = 3;
const int      STARTUP_S      = 5;
const int64_t  DEADLINE_NS    = 75LL * 1000000000LL;

const char LABEL[] = "interest-presence";

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

// produtor: responde enquanto ha interesse. so pode receber desinteresse da RSU
// (o consumidor abandona em silencio), entao o desinteresse prova o broker
class Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Producer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }

    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> producer(_communicator, this);
        std::atomic<bool> stopped{false};
        producer.on_disinterest_received([&stopped](Unit) { stopped.store(true); });

        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (!stopped.load() && Clock::now_ns() < deadline) sleep(1);

        if (!stopped.load()) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL: nunca recebeu desinteresse (RSU nao detectou a saida)" << std::endl;
            std::exit(1);
        }
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] desinteresse recebido da RSU (saida detectada por presenca) -- parando" << std::endl;
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
        while (true) pause();
    }

private:
    int _vm_id;
    uint64_t _seq = 0;
};

// consumidor: assina, coleta algumas respostas e abandona (sai sem desinteresse)
// ao retornar de run(), o processo do componente sai e a VM para de sincronizar
// -> a RSU detecta a ausencia por presenca
class Departing_Consumer : public Component {
public:
    explicit Departing_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> consumer(_communicator, PERIOD_US);

        int got = 0;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (got < NEED_RESPONSES && Clock::now_ns() < deadline) {
            Message * m = consumer.receive_response(2000);
            if (m) { ++got; delete m; }
        }

        if (got < NEED_RESPONSES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL: nao recebeu dados antes de sair (got=" << got << ")" << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] coletei respostas=" << got << " -- ABANDONANDO (sem desinteresse)" << std::endl;
        consumer.abandon(); // NAO envia desinteresse
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
        // run() retorna -> componente sai -> a VM para de sincronizar (PTP)
    }

private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();
    if (vm_id == RSU_VM_ID) { RSU rsu; rsu.initialize(); rsu.run(); return 0; }
    Vehicle vehicle(false);
    if (vm_id == PRODUCER_VM_ID)
        vehicle.add_component(new Producer(vm_id), Component_Ports::TEST_INTEREST_PUB);
    else
        vehicle.add_component(new Departing_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

// etapa 5 -- modo-valor do SmartData: o consumidor le o dado como uma variavel
// viva (value()/operator Value() + fresh()/expired()), sem drenar fila.
// vm1 RSU broker, vm2 produtor (contador), vm3 consumidor em modo-valor

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

const int      VM_COUNT       = 3;
const int      RSU_VM_ID      = 1;
const int      PRODUCER_VM_ID = 2;
const int      CONSUMER_VM_ID = 3;
const uint64_t PERIOD_US      = 300'000;
const int      STARTUP_S      = 5;
const int      SAMPLES        = 6;   // leituras de value() ao longo do tempo

const char LABEL[] = "interest-value";

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

class Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Producer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }

    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> producer(_communicator, this);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
        while (true) pause();
    }
private:
    int _vm_id;
    uint64_t _seq = 0;
};

class Value_Consumer : public Component {
public:
    explicit Value_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_S);
        // modo-valor: NAO enfileira; le o ultimo valor como variavel
        SmartData<Counter_Data> data(_communicator, PERIOD_US, /*auto_refresh=*/true, /*value_mode=*/true);

        // espera ficar fresco (recebeu ao menos um valor)
        for (int i = 0; i < 40 && data.expired(); ++i) sleep(1);
        if (data.expired()) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id << "] FAIL: nunca ficou fresh" << std::endl;
            std::exit(1);
        }

        // le value() periodicamente: tem que estar fresco e o contador avancar
        uint64_t first = static_cast<Counter_Data::Value>(data).seq; // operator Value()
        uint64_t last  = first;
        for (int i = 0; i < SAMPLES; ++i) {
            sleep(1);
            Counter_Data::Value v = data;             // operator Value()
            std::cout << "[" << LABEL << "][vm" << _vm_id
                      << "] value.seq=" << v.seq << " fresh=" << (data.fresh() ? 1 : 0) << std::endl;
            if (!data.fresh()) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id << "] FAIL: ficou stale com produtor ativo" << std::endl;
                std::exit(1);
            }
            last = v.seq;
        }

        if (last <= first) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL: valor nao avancou (" << first << " -> " << last << ")" << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO value avancou " << first << " -> " << last << ", sempre fresh" << std::endl;
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
    if (vm_id == PRODUCER_VM_ID)
        vehicle.add_component(new Producer(vm_id), Component_Ports::TEST_INTEREST_PUB);
    else
        vehicle.add_component(new Value_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

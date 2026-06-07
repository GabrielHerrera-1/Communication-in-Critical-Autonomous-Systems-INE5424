// Etapa 5 -- Mobilidade/handover real (WITH_GPS). vm1 RSU (fixa, master);
// vm2 produtor movel; vm3 consumidor movel. Conforme as VMs trocam de quadrante
// (random-walk do gps.ko), o filtro espacial corta os dados quando o consumidor
// e o produtor NAO estao no mesmo quadrante -> silencio -> o consumidor REANUNCIA
// (re-carimbado com o quadrante novo). Quando voltam a se encontrar, os dados
// fluem. Sem GPS no cliente: a mobilidade e implicita. Cobre T1/T6.

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/iproducer.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/data_types.h"
#include "../src/core/clock.h"
#include "interest_test_utils.h"

#include <iostream>
#include <unistd.h>

namespace {

const char LABEL[]     = "interest-mobility";
const int  VM_COUNT    = 3;
const int  RSU_VM      = 1;
const int  PRODUCER_VM = 2;
const int  CONSUMER_VM = 3;
const uint64_t PERIOD_US = 300'000;
const int  STARTUP_S   = 5;
const int  MAX_WAIT_S  = 80;
const uint64_t NEED_RESPONSES = 3; // recebeu dados (estiveram juntos)
const uint64_t NEED_REISSUES  = 1; // reanunciou (estiveram separados)

class Counter_Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Counter_Producer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    Counter_Data::Value produce() override { return Counter_Data::Value{++_seq}; }
    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> producer(_communicator, this);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
        while (true) pause();
    }
private:
    int _vm_id; uint64_t _seq = 0;
};

class Moving_Consumer : public Component {
public:
    explicit Moving_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> data(_communicator, PERIOD_US);

        for (int i = 0; i < MAX_WAIT_S; ++i) {
            Message * m = data.receive_response(1000);
            if (m) delete m;
            if (data.response_count() >= NEED_RESPONSES && data.reissues() >= NEED_REISSUES)
                break;
        }

        const uint64_t resp = data.response_count();
        const uint64_t reis = data.reissues();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO respostas=" << resp << " reissues=" << reis << std::endl;

        // respostas>0: estiveram no mesmo quadrante (dado fluiu).
        // reissues>0: estiveram separados -> reanuncio re-carimbado (mobilidade).
        if (resp < NEED_RESPONSES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL nunca recebeu dados (nunca co-localizou)" << std::endl;
            std::exit(1);
        }
        if (reis < NEED_REISSUES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL nunca reanunciou (nunca separou) -- mobilidade nao exercitada" << std::endl;
            std::exit(1);
        }
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
    }
private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id(LABEL, VM_COUNT);
    if (vm_id == RSU_VM) { RSU rsu; rsu.initialize(); rsu.run(); return 0; }
    Vehicle vehicle(false);
    if (vm_id == PRODUCER_VM)
        vehicle.add_component(new Counter_Producer(vm_id), Component_Ports::TEST_INTEREST_PUB);
    else
        vehicle.add_component(new Moving_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

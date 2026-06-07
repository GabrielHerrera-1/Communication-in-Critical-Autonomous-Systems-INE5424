// Etapa 5 -- Mobilidade/handover em frota movel (WITH_GPS). vm1 = RSU fixa
// (master). vm2..vm5 = 4 produtores moveis. vm6..vm9 = 4 consumidores moveis.
// Todas as VMs (menos a RSU) fazem random-walk pelos 4 quadrantes (gps.ko).
//
// O filtro espacial da NIC so entrega quadros entre VMs co-localizadas. Entao,
// conforme a frota se mexe, cada consumidor:
//   (a) ora encontra um produtor (recebe dados), ora nao (silencio);
//   (b) ao percorrer os quadrantes, co-localiza com produtores DIFERENTES ao longo
//       do tempo -> agrega >= NEED_DISTINCT produtores distintos;
//   (c) a cada separacao o dado some -> REANUNCIA (re-carimbado pela NIC com o
//       quadrante novo) -> >= NEED_REISSUES reanuncios.
// Sem GPS no cliente: o consumidor nao sabe o quadrante; a mobilidade e implicita.
// TODOS os 4 consumidores precisam validar. Cobre T1/T6 em escala de frota.

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/iproducer.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/data_types.h"
#include "../src/core/clock.h"
#include "interest_test_utils.h"

#include <map>
#include <iostream>
#include <unistd.h>

namespace {

const char LABEL[]       = "interest-mobility";
const int  VM_COUNT      = 9;
const int  RSU_VM        = 1;
const int  FIRST_PROD_VM = 2;   // vm2..vm5
const int  FIRST_CONS_VM = 6;   // vm6..vm9
const uint64_t PERIOD_US = 300'000;
const int  STARTUP_S     = 5;
const int  MAX_WAIT_S    = 100;
const std::size_t NEED_DISTINCT = 2;  // co-localizou com >= 2 produtores (roaming)
const uint64_t    NEED_REISSUES = 2;  // separou >= 2 vezes (reanuncio re-carimbado)

class Counter_Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Counter_Producer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    Counter_Data::Value produce() override {
        return Counter_Data::Value{ (static_cast<uint64_t>(_vm_id) << 32) | (++_seq) };
    }
    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> producer(_communicator, this);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] produtor movel no ar -- cenario validado." << std::endl;
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

        std::map<uint64_t, int> seen;
        for (int i = 0; i < MAX_WAIT_S; ++i) {
            Message * m = data.receive_response(1000);
            if (m) { seen[endpoint_key(m->address())]++; delete m; }
            if (seen.size() >= NEED_DISTINCT && data.reissues() >= NEED_REISSUES) break;
        }

        const uint64_t reis = data.reissues();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores_distintos=" << seen.size()
                  << " reissues=" << reis
                  << " respostas=" << data.response_count() << std::endl;

        if (seen.size() < NEED_DISTINCT) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL co-localizou com < " << NEED_DISTINCT
                      << " produtores -- frota nao agregou ao se mover" << std::endl;
            std::exit(1);
        }
        if (reis < NEED_REISSUES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL reissues=" << reis << " < " << NEED_REISSUES
                      << " -- nunca separou (mobilidade nao exercitada)" << std::endl;
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
    if (vm_id >= FIRST_CONS_VM)
        vehicle.add_component(new Moving_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Counter_Producer(vm_id), Component_Ports::TEST_INTEREST_PUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

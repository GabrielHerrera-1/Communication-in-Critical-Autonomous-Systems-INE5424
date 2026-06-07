// Etapa 5 -- Escala em FROTA MOVEL (WITH_GPS).
//   vm1..vm4 = 4 RSUs, cada uma FIXA num quadrante (0..3) -- e o broker do seu
//             quadrante (rastreamento passivo por presenca + reanuncio local).
//   vm5..vm24 = 20 veiculos que andam LIVREMENTE pelos quadrantes (random-walk do
//             gps.ko, troca a cada ~3s).
//
// Circulam DOIS tipos de dado (variedade): Speed e Counter. Cada veiculo produz OU
// consome um deles. Conforme a frota se move, o filtro espacial da NIC so entrega
// quadros entre VMs co-localizadas -> cada consumidor:
//   (a) so recebe respostas do SEU tipo (demux por Unit, mesmo num trafego misto);
//   (b) agrega produtores DISTINTOS ao reencontra-los pelos quadrantes;
//   (c) ao se separar, o dado some -> REANUNCIA (re-carimbado com o quadrante novo).
//
// Prova, de uma vez: broker por quadrante (4 RSUs) + filtro espacial + mobilidade
// implicita + demux por tipo, em escala de frota. Os 24 VMs precisam validar.

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

const char LABEL[]          = "interest-scale";
const int  VM_COUNT         = 24;            // vm1..4 RSUs + vm5..24 (20 veiculos)
const int  FIRST_VEHICLE_VM = 5;             // vm1..4 sao RSUs (uma por quadrante)
const int  PRODUCER_COUNT   = 12;            // offsets 0..11 produzem; 12..19 consomem
const uint64_t PERIOD_US    = 500'000;
const int  STARTUP_S        = 6;
const std::size_t NEED_DISTINCT = 2;         // co-localizou com >= 2 produtores do seu tipo
const uint64_t    NEED_REISSUES = 2;         // reanunciou >= 2 vezes (mobilidade)
const int64_t DEADLINE_NS   = 250LL * 1000000000LL;

// Produtor movel: serve seu tipo a quem estiver co-localizado. Valida ao subir
// (esta participando); quem prova a troca de dados de fato sao os consumidores.
template <typename Data>
class Fleet_Producer : public Component, public IProducer<typename Data::Value> {
public:
    Fleet_Producer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    typename Data::Value produce() override { return typename Data::Value{}; }
    void run() override {
        sleep(STARTUP_S);
        SmartData<Data> producer(_communicator, this);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] produtor movel (unit=" << static_cast<uint32_t>(Data::UNIT)
                  << ") no ar -- cenario validado." << std::endl;
        while (true) pause();
    }
private:
    int _vm_id;
};

// Consumidor movel: agrega produtores do SEU tipo ao percorrer os quadrantes.
template <typename Data>
class Fleet_Consumer : public Component {
public:
    Fleet_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(STARTUP_S);
        SmartData<Data> data(_communicator, PERIOD_US);

        std::map<uint64_t, int> seen;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(2000);
            if (m) {
                if (!decode_response<Data>(m)) {   // demux: nunca aceita outro tipo
                    std::cerr << "[" << LABEL << "][vm" << _vm_id
                              << "] FAIL recebeu unit errada (demux falhou)" << std::endl;
                    delete m; std::exit(1);
                }
                seen[endpoint_key(m->address())]++;
                delete m;
            }
            if (seen.size() >= NEED_DISTINCT && data.reissues() >= NEED_REISSUES) break;
        }

        const uint64_t reis = data.reissues();
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO unit=" << static_cast<uint32_t>(Data::UNIT)
                  << " produtores_distintos=" << seen.size()
                  << " reissues=" << reis
                  << " respostas=" << data.response_count() << std::endl;

        if (seen.size() < NEED_DISTINCT) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL co-localizou com < " << NEED_DISTINCT
                      << " produtores do seu tipo" << std::endl;
            std::exit(1);
        }
        if (reis < NEED_REISSUES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL reissues=" << reis << " < " << NEED_REISSUES
                      << " (mobilidade nao exercitada)" << std::endl;
            std::exit(1);
        }
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
        while (true) pause();
    }
private:
    int _vm_id;
};

// Adiciona o componente certo (papel x tipo) a partir do offset do veiculo.
void add_vehicle_component(Vehicle & vehicle, int vm_id, int offset) {
    const bool is_speed   = (offset % 2 == 0);        // variedade: par=Speed, impar=Counter
    const bool is_producer = (offset < PRODUCER_COUNT);
    if (is_producer && is_speed)
        vehicle.add_component(new Fleet_Producer<Speed_Data>(vm_id), Component_Ports::TEST_INTEREST_PUB);
    else if (is_producer)
        vehicle.add_component(new Fleet_Producer<Counter_Data>(vm_id), Component_Ports::TEST_INTEREST_PUB);
    else if (is_speed)
        vehicle.add_component(new Fleet_Consumer<Speed_Data>(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Fleet_Consumer<Counter_Data>(vm_id), Component_Ports::TEST_INTEREST_SUB);
}

} // namespace

int main() {
    const int vm_id = detect_vm_id(LABEL, VM_COUNT);

    // vm1..4 = RSUs (o harness ancora cada uma num quadrante; o is_master congela).
    if (vm_id < FIRST_VEHICLE_VM) {
        RSU rsu;
        rsu.initialize();
        rsu.run();
        return 0;
    }

    Vehicle vehicle(false);
    add_vehicle_component(vehicle, vm_id, vm_id - FIRST_VEHICLE_VM);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

// etapa 5 -- escala em frota movel com veiculos multi componente
//   vm1..vm4 = 4 RSUs, cada uma fixa num quadrante (0..3) -- broker do seu
//             quadrante (rastreamento passivo + reanuncio local)
//   vm5..vm24 = 20 veiculos que andam livremente pelos quadrantes (random-walk)
//
// cada veiculo tem 5 componentes (como um veiculo autonomo real): 4 sensores
// produtores (Speed, Lidar, Radar, Counter) + 1 consumidor-controlador. os quatro
// tipos sao ativamente consumidos pela frota (variedade plena: o consumidor de
// cada veiculo rotaciona entre Speed/Lidar/Radar/Counter), entao todos os 5
// componentes de cada veiculo participam. conforme a frota se move, o filtro
// espacial da NIC so entrega quadros entre VMs co-localizadas -> cada consumidor:
//   (a) so recebe respostas do seu tipo (demux por Unit em trafego misto)
//   (b) agrega produtores distintos ao reencontra-los pelos quadrantes
//   (c) ao se separar, reanuncia (re-carimbado com o quadrante novo)
//
// prova de uma vez: broker por quadrante (4 RSUs) + filtro espacial + mobilidade
// + demux por tipo + veiculos multi-componente, em escala. cada VM e validada pelo
// seu consumidor (os produtores ficam em silencio, para nao mascarar). O consumidor
// retorna apos validar, liberando CPU

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
const int  FIRST_VEHICLE_VM = 5;
// 5 comp/VM e 4 tipos ativos (~120 processos) -> periodo longo segura a taxa
// total de frames (todo componente processa cada frame do seu quadrante).
const uint64_t PERIOD_US    = 1'200'000;
const std::size_t NEED_DISTINCT = 2;         // co-localizou com >= 2 produtores do seu tipo
const uint64_t    NEED_REISSUES = 2;         // reanunciou >= 2 vezes (mobilidade)
const int64_t DEADLINE_NS   = 280LL * 1000000000LL;

using Port = Component_Ports::Port;
const Port PORT_SPEED   = 0xF503;
const Port PORT_LIDAR   = 0xF504;
const Port PORT_RADAR   = 0xF505;
const Port PORT_COUNTER = Component_Ports::TEST_INTEREST_PUB;  // 0xF501
const Port PORT_SUB     = Component_Ports::TEST_INTEREST_SUB;  // 0xF502

// sensor-produtor (serve em silencio, quem valida a VM e o consumidor dela)
template <typename Data>
class Fleet_Producer : public Component, public IProducer<typename Data::Value> {
public:
    Fleet_Producer(int vm_id, Port port) : Component(LABEL), _vm_id(vm_id), _port(port) {}
    void initialize() override {}
    Port logical_port() const override { return _port; }
    typename Data::Value produce() override { return typename Data::Value{}; }
    void run() override {
        sleep(6);
        SmartData<Data> producer(this->_communicator, this);
        while (true) pause();
    }
private:
    int _vm_id; Port _port;
};

// consumidor-controlador movel: agrega produtores do seu tipo ao percorrer os
// quadrantes. 
template <typename Data>
class Fleet_Consumer : public Component {
public:
    Fleet_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return PORT_SUB; }

    void run() override {
        sleep(8);
        SmartData<Data> data(this->_communicator, PERIOD_US);

        std::map<uint64_t, int> seen;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(2000);
            if (m) {
                if (!decode_response<Data>(m)) {           // demux: nunca outro tipo
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
        // retorna: o consumidor sai e libera CPU, os 4 sensores da VM seguem no ar
    }
private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id(LABEL, VM_COUNT);

    // vm1..4 = RSUs (o harness ancora cada uma num quadrante; is_master congela)
    if (vm_id < FIRST_VEHICLE_VM) {
        RSU rsu; rsu.initialize(); rsu.run();
        return 0;
    }

    const int offset = vm_id - FIRST_VEHICLE_VM;   // 0..19
    Vehicle vehicle(false);

    // 4 sensores-produtores em todo veiculo (Speed e Counter sao consumidos pela
    // frota, Lidar e Radar ficam a bordo, prontos, sem assinante neste cenario)
    vehicle.add_component(new Fleet_Producer<Speed_Data>(vm_id, PORT_SPEED), PORT_SPEED);
    vehicle.add_component(new Fleet_Producer<Lidar_Data>(vm_id, PORT_LIDAR), PORT_LIDAR);
    vehicle.add_component(new Fleet_Producer<Radar_Data>(vm_id, PORT_RADAR), PORT_RADAR);
    vehicle.add_component(new Fleet_Producer<Counter_Data>(vm_id, PORT_COUNTER), PORT_COUNTER);


    switch (offset % 4)
    {
    case 0:
        vehicle.add_component(new Fleet_Consumer<Speed_Data>(vm_id), PORT_SUB);
        break;
    case 1:
        vehicle.add_component(new Fleet_Consumer<Lidar_Data>(vm_id), PORT_SUB);
        break;
    case 2:
        vehicle.add_component(new Fleet_Consumer<Radar_Data>(vm_id), PORT_SUB);
        break;
    case 3:
        vehicle.add_component(new Fleet_Consumer<Counter_Data>(vm_id), PORT_SUB);
        break;
    }

    vehicle.initialize();
    vehicle.run();
    return 0;
}

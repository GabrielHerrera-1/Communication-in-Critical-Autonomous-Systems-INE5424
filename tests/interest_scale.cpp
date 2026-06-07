// Etapa 5 -- Escala: FROTA de 20 veiculos, cada um com 5 COMPONENTES (como um
// veiculo autonomo real: varios sensores + um controlador). vm1 = RSU/broker;
// vm2..vm21 = 20 veiculos. Cada veiculo roda 4 produtores-sensores (Speed, Lidar,
// Radar, Counter) + um 5o componente:
//   - 10 veiculos (dual-role): o 5o e um CONSUMIDOR de Counter que precisa
//     AGREGAR >= MIN_PRODUCERS produtores Counter distintos da frota;
//   - 10 veiculos (so-sensores): o 5o e mais um produtor.
// So Counter e consumido (ativo na frota); Speed/Lidar/Radar ficam a bordo
// prontos (sem assinante neste cenario, como sensores reais ociosos) -- isso
// mantem a taxa de frames baixa apesar dos ~105 componentes.
//
// Quem valida cada VM: o consumidor (dual-role) ou o produtor Counter (so-sensores,
// anuncia ao servir a 1a resposta). Assim os 21 VMs precisam participar de fato.
// 10 consumidores no total mantem a contencao de CPU sob controle com 21 VMs.

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
const int  VM_COUNT         = 21;            // vm1 RSU + 20 veiculos
const int  RSU_VM_ID        = 1;
const int  FIRST_VEHICLE_VM = 2;
const int  DUAL_ROLE_COUNT  = 10;            // primeiros 10 veiculos tambem consomem
// 4s: com 5 componentes/VM (~105 processos), TODO componente processa cada frame
// broadcast; periodo longo segura a taxa total (so Counter e consumido => ~5 frames/s).
const uint64_t PERIOD_US    = 4'000'000;
const std::size_t MIN_PRODUCERS = 5;         // cada consumidor agrega >= isso (de 20)
const int  MIN_SAMPLES      = 2;
const int64_t DEADLINE_NS   = 280LL * 1000000000LL;

// Portas (uma por componente do veiculo; reaproveitadas entre veiculos -- as
// respostas sao broadcast e o match e por Unit).
using Port = Component_Ports::Port;
const Port PORT_SPEED   = 0xF503;
const Port PORT_LIDAR   = 0xF504;
const Port PORT_RADAR   = 0xF505;
const Port PORT_COUNTER = Component_Ports::TEST_INTEREST_PUB;  // 0xF501
const Port PORT_EXTRA   = 0xF506;
const Port PORT_SUB     = Component_Ports::TEST_INTEREST_SUB;  // 0xF502

int producers_ready(const std::map<uint64_t, int> & by_producer, int min_samples) {
    int n = 0;
    for (const auto & kv : by_producer) if (kv.second >= min_samples) ++n;
    return n;
}

// Produtor-sensor. announce=true imprime validado na 1a resposta (usado pelo
// veiculo so-sensores para validar a VM); dual-role usa announce=false.
template <typename Data>
class Fleet_Producer : public Component, public IProducer<typename Data::Value> {
public:
    Fleet_Producer(int vm_id, Port port, int start_s, bool announce)
        : Component(LABEL), _vm_id(vm_id), _port(port), _start_s(start_s), _announce(announce) {}
    void initialize() override {}
    Port logical_port() const override { return _port; }
    typename Data::Value produce() override { return typename Data::Value{}; }
    void run() override {
        sleep(_start_s);
        SmartData<Data> producer(this->_communicator, this);
        if (_announce) {
            producer.on_response_sent([this](uint64_t n) {
                if (n == 1)
                    std::cout << "[" << LABEL << "][vm" << _vm_id
                              << "] sensor servindo a frota -- cenario validado." << std::endl;
            });
        }
        while (true) pause();
    }
private:
    int _vm_id; Port _port; int _start_s; bool _announce;
};

// Consumidor-controlador: agrega respostas da sua Unit de muitos produtores.
template <typename Data>
class Fleet_Consumer : public Component {
public:
    Fleet_Consumer(int vm_id, int start_s) : Component(LABEL), _vm_id(vm_id), _start_s(start_s) {}
    void initialize() override {}
    Port logical_port() const override { return PORT_SUB; }

    void run() override {
        sleep(_start_s);
        SmartData<Data> data(this->_communicator, PERIOD_US);

        std::map<uint64_t, int> by_producer;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(3000);
            if (!m) continue;
            if (!decode_response<Data>(m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id << "] FAIL unit errada (demux)" << std::endl;
                delete m; std::exit(1);
            }
            by_producer[endpoint_key(m->address())]++;
            delete m;
            if (producers_ready(by_producer, MIN_SAMPLES) >= static_cast<int>(MIN_PRODUCERS)) break;
        }

        const int ready = producers_ready(by_producer, MIN_SAMPLES);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO unit=" << static_cast<uint32_t>(Data::UNIT)
                  << " produtores_distintos=" << by_producer.size()
                  << " prontos=" << ready << " minimo=" << MIN_PRODUCERS
                  << " respostas=" << data.response_count() << std::endl;

        if (ready < static_cast<int>(MIN_PRODUCERS)) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL prontos=" << ready << " < " << MIN_PRODUCERS << std::endl;
            std::exit(1);
        }
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] controlador agregou a frota -- cenario validado." << std::endl;
        while (true) pause();
    }
private:
    int _vm_id; int _start_s;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id(LABEL, VM_COUNT);
    if (vm_id == RSU_VM_ID) { RSU rsu; rsu.initialize(); rsu.run(); return 0; }

    const int offset = vm_id - FIRST_VEHICLE_VM;       // 0..19
    const bool dual_role = (offset < DUAL_ROLE_COUNT); // primeiros 10 tambem consomem
    const int ps = 5 + (offset % 6);                   // start dos sensores
    Vehicle vehicle(false);

    // 4 sensores-produtores em TODO veiculo (Speed e Counter sao consumidos pela
    // frota; Lidar e Radar ficam prontos sem assinante neste cenario).
    vehicle.add_component(new Fleet_Producer<Speed_Data>(vm_id, PORT_SPEED, ps, false), PORT_SPEED);
    vehicle.add_component(new Fleet_Producer<Lidar_Data>(vm_id, PORT_LIDAR, ps, false), PORT_LIDAR);
    vehicle.add_component(new Fleet_Producer<Radar_Data>(vm_id, PORT_RADAR, ps, false), PORT_RADAR);
    // Counter: nos so-sensores e ele quem ANUNCIA a validacao da VM (esta ativo).
    vehicle.add_component(new Fleet_Producer<Counter_Data>(vm_id, PORT_COUNTER, ps, !dual_role), PORT_COUNTER);

    // 5o componente
    if (dual_role) {
        const int cs = 14 + (offset % 6);
        // 10 consumidores de Counter (so esse tipo e consumido -> taxa de frames baixa)
        vehicle.add_component(new Fleet_Consumer<Counter_Data>(vm_id, cs), PORT_SUB);
    } else {
        // 5o sensor (Radar extra, a bordo)
        vehicle.add_component(new Fleet_Producer<Radar_Data>(vm_id, PORT_EXTRA, ps, false), PORT_EXTRA);
    }

    vehicle.initialize();
    vehicle.run();
    return 0;
}

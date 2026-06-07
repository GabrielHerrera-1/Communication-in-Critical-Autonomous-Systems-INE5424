// Etapa 5 -- Escala: FROTA de 20 veiculos. vm1 = RSU/broker; vm2..vm21 = 20
// veiculos. TODOS os 20 produzem Counter (valor codifica o vm_id de origem), entao
// ha uma frota real de 20 produtores distintos. Metade dos veiculos (10) e
// DUAL-ROLE: alem de produzir, roda um consumidor que precisa AGREGAR >=
// MIN_PRODUCERS produtores distintos da frota. A outra metade e so-produtora.
//
// Quem valida cada VM:
//   - veiculo dual-role: o CONSUMIDOR (so passa se agregou a frota; o produtor
//     fica em silencio para nao mascarar uma falha do consumidor da mesma VM);
//   - veiculo so-produtor: o PRODUTOR (imprime validado ao enviar a 1a resposta,
//     provando que ouviu o interesse e esta servindo).
// Assim TODOS os 21 VMs precisam participar de fato para o teste passar.
//
// 10 consumidores (em vez de 20) mantem a contencao de CPU sob controle com 21
// VMs; periodo de 2s segura a taxa de frames na frota.

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
const uint64_t PERIOD_US    = 2'000'000;     // 2s: taxa baixa, escala estavel
const std::size_t MIN_PRODUCERS = 6;         // cada consumidor agrega >= isso (de 20)
const int  MIN_SAMPLES      = 2;
const int64_t DEADLINE_NS   = 230LL * 1000000000LL;

int producers_ready(const std::map<uint64_t, int> & by_producer, int min_samples) {
    int n = 0;
    for (const auto & kv : by_producer) if (kv.second >= min_samples) ++n;
    return n;
}

// Produtor da frota. announce=true (so-produtor) imprime validado na 1a resposta;
// announce=false (dual-role) fica em silencio (quem valida a VM e o consumidor).
class Fleet_Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    Fleet_Producer(int vm_id, int start_s, bool announce)
        : Component(LABEL), _vm_id(vm_id), _start_s(start_s), _announce(announce) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    Counter_Data::Value produce() override {
        return Counter_Data::Value{ (static_cast<uint64_t>(_vm_id) << 32) | (++_seq) };
    }
    void run() override {
        sleep(_start_s);
        SmartData<Counter_Data> producer(_communicator, this);
        if (_announce) {
            producer.on_response_sent([this](uint64_t n) {
                if (n == 1)
                    std::cout << "[" << LABEL << "][vm" << _vm_id
                              << "] produtor servindo a frota -- cenario validado." << std::endl;
            });
        }
        while (true) pause();
    }
private:
    int _vm_id; int _start_s; bool _announce; uint64_t _seq = 0;
};

// Consumidor da frota: agrega respostas de muitos produtores distintos.
class Fleet_Consumer : public Component {
public:
    Fleet_Consumer(int vm_id, int start_s) : Component(LABEL), _vm_id(vm_id), _start_s(start_s) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }

    void run() override {
        sleep(_start_s);
        SmartData<Counter_Data> data(_communicator, PERIOD_US);

        std::map<uint64_t, int> by_producer;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(3000);
            if (!m) continue;
            if (!decode_response<Counter_Data>(m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id << "] FAIL unit errada" << std::endl;
                delete m; std::exit(1);
            }
            by_producer[endpoint_key(m->address())]++;
            delete m;
            if (producers_ready(by_producer, MIN_SAMPLES) >= static_cast<int>(MIN_PRODUCERS)) break;
        }

        const int ready = producers_ready(by_producer, MIN_SAMPLES);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores_distintos=" << by_producer.size()
                  << " prontos=" << ready << " minimo=" << MIN_PRODUCERS
                  << " respostas=" << data.response_count() << std::endl;

        if (ready < static_cast<int>(MIN_PRODUCERS)) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL prontos=" << ready << " < " << MIN_PRODUCERS << std::endl;
            std::exit(1);
        }
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] consumidor agregou a frota -- cenario validado." << std::endl;
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

    Vehicle vehicle(false);
    // Todo veiculo PRODUZ. Produtor anuncia validado apenas quando NAO ha
    // consumidor na mesma VM (nos dual-role quem valida e o consumidor).
    vehicle.add_component(new Fleet_Producer(vm_id, 5 + (offset % 6), /*announce=*/!dual_role),
                          Component_Ports::TEST_INTEREST_PUB);
    if (dual_role) {
        vehicle.add_component(new Fleet_Consumer(vm_id, 14 + (offset % 6)),
                              Component_Ports::TEST_INTEREST_SUB);
    }
    vehicle.initialize();
    vehicle.run();
    return 0;
}

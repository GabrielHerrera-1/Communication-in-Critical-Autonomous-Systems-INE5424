// etapa 5 -- fan-out: N consumidores <- 1 produtor, com dois periodos
// vm1 RSU, vm2 produtor, vm3,vm4 consumidores rapidos (250ms), vm5,vm6 lentos
// (500ms). valida: (a) broadcast 1->N (todos recebem), (b) o produtor agrega e
// responde com uma thread no periodo mais curto (250ms)

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

const char LABEL[]   = "interest-fanout";
const int  VM_COUNT  = 6;
const int  RSU_VM    = 1;
const int  PRODUCER_VM = 2;
const uint64_t PERIOD_FAST = 250'000;  // vm3, vm4
const uint64_t PERIOD_SLOW = 500'000;  // vm5, vm6
const int  STARTUP_S = 5;
const int  WINDOW_S  = 12;             // janela de medicao do produtor
const int  COLLECT_S = 26;             // consumidor fica assinando este tempo
const int  MIN_CONSUMER_SAMPLES = 12;  // cada consumidor deve coletar isso

// produtor: serve todos os interesses da Unit. mede a propria taxa de resposta
// para provar que ha uma thread no periodo mais curto (250ms -> ~48 em 12s),
// e nao duas threads somando as cadencias (~72 em 12s)
class Counter_Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Counter_Producer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    Counter_Data::Value produce() override { return Counter_Data::Value{++_seq}; }

    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> producer(_communicator, this);

        // deixa os consumidores subirem e os interesses chegarem
        sleep(8);
        uint64_t r0 = producer.responses_sent();
        sleep(WINDOW_S);
        uint64_t count = producer.responses_sent() - r0;

        // 1 thread  250ms em 12s ~ 48. 2 threads (250+500) ~ 72. o limite
        // superior separa os dois casos, o inferior garante que produziu
        const uint64_t one_thread = static_cast<uint64_t>(WINDOW_S) * 1000000ULL / PERIOD_FAST; // 48
        const uint64_t lo = one_thread * 6 / 10;   // ~28
        const uint64_t hi = one_thread * 13 / 10;  // ~62  (< 72 das duas threads)

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO respostas_na_janela=" << count
                  << " esperado~" << one_thread << " (1 thread no periodo mais curto)" << std::endl;

        if (count < lo || count > hi) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL taxa=" << count << " fora de [" << lo << "," << hi
                      << "] -- nao agregou numa thread no periodo mais curto" << std::endl;
            std::exit(1);
        }
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
        while (true) pause();
    }

private:
    int _vm_id;
    uint64_t _seq = 0;
};

class Counter_Consumer : public Component {
public:
    Counter_Consumer(int vm_id, uint64_t period_us)
        : Component(LABEL), _vm_id(vm_id), _period_us(period_us) {}
    void initialize() override {}
    // porta distinta por VM para identidade no broker (todos consomem a mesma Unit)
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB + _vm_id; }

    void run() override {
        sleep(STARTUP_S);
        SmartData<Counter_Data> data(_communicator, _period_us);

        // coleta por COLLECT_S: o consumidor PERMANECE assinado durante toda a
        // janela de medicao do produtor (se saisse cedo, mandaria desinteresse e
        // o produtor pararia antes de medir)
        int got = 0;
        const int64_t end = Clock::now_ns() + static_cast<int64_t>(COLLECT_S) * 1000000000LL;
        while (Clock::now_ns() < end) {
            Message * m = data.receive_response(2000);
            if (!m) continue;
            if (!decode_response<Counter_Data>(m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id << "] FAIL tipo inesperado" << std::endl;
                delete m; std::exit(1);
            }
            ++got;
            delete m;
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO periodo_pedido=" << _period_us
                  << " respostas=" << got << std::endl;

        if (got < MIN_CONSUMER_SAMPLES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL respostas=" << got << " < " << MIN_CONSUMER_SAMPLES << std::endl;
            std::exit(1);
        }
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
        while (true) pause(); // fica vivo: nao manda desinteresse durante a medicao
    }

private:
    int _vm_id;
    uint64_t _period_us;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id(LABEL, VM_COUNT);
    if (vm_id == RSU_VM) { RSU rsu; rsu.initialize(); rsu.run(); return 0; }

    Vehicle vehicle(false);
    if (vm_id == PRODUCER_VM) {
        vehicle.add_component(new Counter_Producer(vm_id), Component_Ports::TEST_INTEREST_PUB);
    } else {
        const uint64_t period = (vm_id <= 4) ? PERIOD_FAST : PERIOD_SLOW;
        vehicle.add_component(new Counter_Consumer(vm_id, period),
                              Component_Ports::TEST_INTEREST_SUB + vm_id);
    }
    vehicle.initialize();
    vehicle.run();
    return 0;
}

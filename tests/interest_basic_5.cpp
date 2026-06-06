// Etapa 5 -- Teste 1: fluxo minimo com varios produtores.
// vm1 RSU; vm2 consumidor; vm3..vm7 produtores de TEST_COUNTER.

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

const char LABEL[] = "interest-basic-5";
const int VM_COUNT = 7;
const int RSU_VM_ID = 1;
const int CONSUMER_VM_ID = 2;
const std::size_t EXPECTED_PRODUCERS = 5;
const uint64_t PERIOD_US = 300'000;
const int STARTUP_DELAY_S = 5;
const int64_t DEADLINE_NS = 90LL * 1000000000LL;

class Counter_Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Counter_Producer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    bool subscribe_logical_broadcast() const override { return false; }

    Counter_Data::Value produce() override { return Counter_Data::Value{++_seq}; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        SmartData<Counter_Data> data(_communicator, this);
        data.on_response_sent([this](uint64_t n) {
            if (n == 2) {
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] produtor enviou multiplas respostas" << std::endl;
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] cenario validado." << std::endl;
            }
        });
        while (true) pause();
    }

private:
    int _vm_id;
    uint64_t _seq = 0;
};

class Counter_Consumer : public Component {
public:
    explicit Counter_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }
    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        SmartData<Counter_Data> data(_communicator, PERIOD_US);

        std::map<uint64_t, int> responses_by_producer;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(2000);
            if (!m) continue;

            bool ok = decode_response<Counter_Data>(m);
            if (!ok) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL resposta com tipo inesperado" << std::endl;
                delete m;
                std::exit(1);
            }

            responses_by_producer[endpoint_key(m->address())]++;
            delete m;

            bool enough = responses_by_producer.size() >= EXPECTED_PRODUCERS;
            for (const auto & kv : responses_by_producer)
                enough = enough && kv.second >= 2;
            if (enough) break;
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores=" << responses_by_producer.size()
                  << " respostas=" << data.response_count() << std::endl;

        if (responses_by_producer.size() < EXPECTED_PRODUCERS) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL produtores=" << responses_by_producer.size()
                      << " esperado=" << EXPECTED_PRODUCERS << std::endl;
            std::exit(1);
        }

        for (const auto & kv : responses_by_producer) {
            if (kv.second < 2) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL produtor com menos de 2 respostas" << std::endl;
                std::exit(1);
            }
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id(LABEL, VM_COUNT);
    if (vm_id == RSU_VM_ID) {
        RSU rsu;
        rsu.initialize();
        rsu.run();
        return 0;
    }

    Vehicle vehicle(false);
    if (vm_id == CONSUMER_VM_ID)
        vehicle.add_component(new Counter_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Counter_Producer(vm_id), Component_Ports::TEST_INTEREST_PUB);

    vehicle.initialize();
    vehicle.run();
    return 0;
}

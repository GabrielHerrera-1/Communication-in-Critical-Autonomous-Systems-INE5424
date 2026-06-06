// Etapa 5 -- Teste 2: periodicidade em escala.
// vm1 RSU; vm2 consumidor; vm3..vm22 produtores de TEST_COUNTER.

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/iproducer.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/data_types.h"
#include "../src/core/clock.h"
#include "interest_test_utils.h"

#include <map>
#include <vector>
#include <iostream>
#include <unistd.h>

namespace {

const char LABEL[] = "interest-period-scale";
const int VM_COUNT = 22;
const int RSU_VM_ID = 1;
const int CONSUMER_VM_ID = 2;
const std::size_t EXPECTED_PRODUCERS = 20;
const uint64_t PERIOD_US = 500'000;
const int SAMPLES_PER_PRODUCER = 6;
const int WARMUP = 1;
const double LOW_FACTOR = 0.5;
const double HIGH_FACTOR = 2.2;
const int STARTUP_DELAY_S = 8;
const int64_t DEADLINE_NS = 180LL * 1000000000LL;

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
            if (n == SAMPLES_PER_PRODUCER) {
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

class Period_Consumer : public Component {
public:
    explicit Period_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }
    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        SmartData<Counter_Data> data(_communicator, PERIOD_US);

        std::map<uint64_t, std::vector<int64_t>> timestamps_by_producer;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(2000);
            if (!m) continue;

            if (!decode_response<Counter_Data>(m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL resposta com tipo inesperado" << std::endl;
                delete m;
                std::exit(1);
            }

            timestamps_by_producer[endpoint_key(m->address())].push_back(m->timestamp());
            delete m;

            bool enough = timestamps_by_producer.size() >= EXPECTED_PRODUCERS;
            for (const auto & kv : timestamps_by_producer)
                enough = enough && static_cast<int>(kv.second.size()) >= SAMPLES_PER_PRODUCER;
            if (enough) break;
        }

        if (timestamps_by_producer.size() < EXPECTED_PRODUCERS) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL produtores=" << timestamps_by_producer.size()
                      << " esperado=" << EXPECTED_PRODUCERS << std::endl;
            std::exit(1);
        }

        const int64_t low = static_cast<int64_t>(LOW_FACTOR * PERIOD_US);
        const int64_t high = static_cast<int64_t>(HIGH_FACTOR * PERIOD_US);
        int64_t worst_avg = 0;
        int checked = 0;
        for (const auto & kv : timestamps_by_producer) {
            if (static_cast<int>(kv.second.size()) < SAMPLES_PER_PRODUCER) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL produtor com poucas amostras" << std::endl;
                std::exit(1);
            }

            std::vector<int64_t> deltas_us;
            for (std::size_t i = 1; i < kv.second.size(); ++i)
                deltas_us.push_back((kv.second[i] - kv.second[i - 1]) / 1000);
            int64_t avg = average_after_warmup(deltas_us, WARMUP);
            if (avg > worst_avg) worst_avg = avg;
            ++checked;

            if (avg < low || avg > high) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL avg_us=" << avg
                          << " fora de [" << low << "," << high << "]" << std::endl;
                std::exit(1);
            }
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores=" << checked
                  << " period_us=" << PERIOD_US
                  << " worst_avg_us=" << worst_avg
                  << " respostas=" << data.response_count() << std::endl;
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
        vehicle.add_component(new Period_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Counter_Producer(vm_id), Component_Ports::TEST_INTEREST_PUB);

    vehicle.initialize();
    vehicle.run();
    return 0;
}

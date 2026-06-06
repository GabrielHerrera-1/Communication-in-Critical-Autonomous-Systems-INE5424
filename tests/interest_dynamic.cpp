// Etapa 5 -- Teste 4: entrada e saida dinamica.
// A RSU repete interesses. Um consumidor sai, outro continua; produtores tardios
// entram depois e precisam responder sem reiniciar a simulacao.

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

const char LABEL[] = "interest-dynamic";
const int VM_COUNT = 7;
const int RSU_VM_ID = 1;
const int CONSUMER_A_VM = 2;
const int CONSUMER_B_VM = 3;
const int FIRST_LATE_PRODUCER_VM = 6;
const uint64_t PERIOD_US = 300'000;
const int EARLY_START_S = 5;
const int CONSUMER_B_START_S = 12;
const int CONSUMER_A_UNSUB_S = 18;
const int LATE_PRODUCER_START_S = 22;
const std::size_t EXPECTED_TOTAL_PRODUCERS = 4;
const int64_t DEADLINE_NS = 90LL * 1000000000LL;

void sleep_until_second(int target_s, int started_s = 0) {
    if (target_s > started_s) sleep(target_s - started_s);
}

class Dynamic_Producer : public Component, public IProducer<Counter_Data::Value> {
public:
    Dynamic_Producer(int vm_id, int start_s)
        : Component(LABEL), _vm_id(vm_id), _start_s(start_s) {}

    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    bool subscribe_logical_broadcast() const override { return false; }
    Counter_Data::Value produce() override { return Counter_Data::Value{++_seq}; }

    void run() override {
        sleep(_start_s);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] produtor entrou em t~" << _start_s << "s" << std::endl;

        SmartData<Counter_Data> data(_communicator, this);
        data.on_response_sent([this](uint64_t n) {
            if (n == 1) {
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] cenario validado." << std::endl;
            }
        });
        while (true) pause();
    }

private:
    int _vm_id;
    int _start_s;
    uint64_t _seq = 0;
};

class Leaving_Consumer : public Component {
public:
    explicit Leaving_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }
    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        sleep(EARLY_START_S);
        SmartData<Counter_Data> data(_communicator, PERIOD_US, /*auto_refresh=*/false);

        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (data.response_count() < 4 && Clock::now_ns() < deadline) {
            Message * m = data.receive_response(2000);
            if (m) {
                if (!decode_response<Counter_Data>(m)) {
                    std::cerr << "[" << LABEL << "][vm" << _vm_id
                              << "] FAIL tipo inesperado antes do desinteresse" << std::endl;
                    delete m;
                    std::exit(1);
                }
                delete m;
            }
        }

        sleep_until_second(CONSUMER_A_UNSUB_S, EARLY_START_S);
        data.unsubscribe();

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO saiu_com_respostas=" << data.response_count()
                  << " desinteresse=enviado" << std::endl;
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

class Continuing_Consumer : public Component {
public:
    explicit Continuing_Consumer(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB + 1; }
    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        sleep(CONSUMER_B_START_S);
        // Este consumidor permanece vivo, entao usa o comportamento normal do
        // SmartData: reenvia periodicamente o interesse enquanto estiver ativo.
        SmartData<Counter_Data> data(_communicator, PERIOD_US);

        std::map<uint64_t, int> by_producer;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(3000);
            if (!m) continue;

            if (!decode_response<Counter_Data>(m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL tipo inesperado no consumidor persistente" << std::endl;
                delete m;
                std::exit(1);
            }

            by_producer[endpoint_key(m->address())]++;
            delete m;

            bool enough = by_producer.size() >= EXPECTED_TOTAL_PRODUCERS;
            for (const auto & kv : by_producer) enough = enough && kv.second >= 2;
            if (enough) break;
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO produtores_pos_saida=" << by_producer.size()
                  << " respostas=" << data.response_count() << std::endl;

        if (by_producer.size() < EXPECTED_TOTAL_PRODUCERS) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL produtor tardio/desinteresse quebrou continuidade" << std::endl;
            std::exit(1);
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
    if (vm_id == CONSUMER_A_VM) {
        vehicle.add_component(new Leaving_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB);
    } else if (vm_id == CONSUMER_B_VM) {
        vehicle.add_component(new Continuing_Consumer(vm_id), Component_Ports::TEST_INTEREST_SUB + 1);
    } else {
        int start_s = (vm_id >= FIRST_LATE_PRODUCER_VM) ? LATE_PRODUCER_START_S : EARLY_START_S;
        vehicle.add_component(new Dynamic_Producer(vm_id, start_s), Component_Ports::TEST_INTEREST_PUB);
    }

    vehicle.initialize();
    vehicle.run();
    return 0;
}

// Etapa 5 -- Teste 3: varios tipos ao mesmo tempo.
// Consumidores de SPEED, LIDAR, RADAR e TEST_COUNTER recebem apenas sua Unit.

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

const char LABEL[] = "interest-mixed-types";
const int VM_COUNT = 12;
const int RSU_VM_ID = 1;
const int STARTUP_DELAY_S = 6;
const int64_t DEADLINE_NS = 120LL * 1000000000LL;

template <typename Data>
typename Data::Value make_value(int, uint64_t);

template <>
Speed_Data::Value make_value<Speed_Data>(int vm_id, uint64_t seq) {
    return Speed_Data::Value{static_cast<double>(vm_id) + static_cast<double>(seq) / 1000.0};
}

template <>
Lidar_Data::Value make_value<Lidar_Data>(int vm_id, uint64_t seq) {
    return Lidar_Data::Value{10.0 + static_cast<double>(vm_id) + static_cast<double>(seq)};
}

template <>
Radar_Data::Value make_value<Radar_Data>(int vm_id, uint64_t seq) {
    return Radar_Data::Value{20.0 + static_cast<double>(vm_id) + static_cast<double>(seq)};
}

template <>
Counter_Data::Value make_value<Counter_Data>(int, uint64_t seq) {
    return Counter_Data::Value{seq};
}

template <typename Data>
class Typed_Producer : public Component, public IProducer<typename Data::Value> {
public:
    Typed_Producer(const char * label, int vm_id, Port port)
        : Component(label), _vm_id(vm_id), _port_id(port) {}

    void initialize() override {}
    Port logical_port() const override { return _port_id; }
    bool subscribe_logical_broadcast() const override { return false; }
    typename Data::Value produce() override { return make_value<Data>(_vm_id, ++_seq); }

    void run() override {
        sleep(STARTUP_DELAY_S);
        SmartData<Data> data(_communicator, this);
        data.on_response_sent([this](uint64_t n) {
            if (n == 1) {
                std::cout << "[" << LABEL << "][vm" << _vm_id
                          << "] produtor unit=" << static_cast<uint32_t>(Data::UNIT)
                          << " cenario validado." << std::endl;
            }
        });
        while (true) pause();
    }

private:
    int _vm_id;
    Port _port_id;
    uint64_t _seq = 0;
};

template <typename Data>
class Typed_Consumer : public Component {
public:
    Typed_Consumer(const char * label, int vm_id, Port port,
                   uint64_t period_us, std::size_t expected_producers)
        : Component(label), _vm_id(vm_id), _port_id(port),
          _period_us(period_us), _expected_producers(expected_producers) {}

    void initialize() override {}
    Port logical_port() const override { return _port_id; }
    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        SmartData<Data> data(_communicator, _period_us);

        std::map<uint64_t, int> by_producer;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(2000);
            if (!m) continue;

            if (!decode_response<Data>(m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL consumidor aceitou unit errada" << std::endl;
                delete m;
                std::exit(1);
            }

            by_producer[endpoint_key(m->address())]++;
            delete m;

            bool enough = by_producer.size() >= _expected_producers;
            for (const auto & kv : by_producer) enough = enough && kv.second >= 2;
            if (enough) break;
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO unit=" << static_cast<uint32_t>(Data::UNIT)
                  << " produtores=" << by_producer.size()
                  << " respostas=" << data.response_count() << std::endl;

        if (by_producer.size() < _expected_producers) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL produtores=" << by_producer.size()
                      << " esperado=" << _expected_producers << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
    Port _port_id;
    uint64_t _period_us;
    std::size_t _expected_producers;
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
    switch (vm_id) {
    case 2:
        vehicle.add_component(new Typed_Consumer<Speed_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_SUB, 100'000, 2));
        break;
    case 3:
        vehicle.add_component(new Typed_Consumer<Lidar_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_SUB, 250'000, 2));
        break;
    case 4:
        vehicle.add_component(new Typed_Consumer<Radar_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_SUB, 500'000, 1));
        break;
    case 5:
        vehicle.add_component(new Typed_Consumer<Counter_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_SUB, 1'000'000, 2));
        break;
    case 6:
    case 7:
        vehicle.add_component(new Typed_Producer<Speed_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_PUB));
        break;
    case 8:
    case 9:
        vehicle.add_component(new Typed_Producer<Lidar_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_PUB));
        break;
    case 10:
        vehicle.add_component(new Typed_Producer<Radar_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_PUB));
        break;
    default:
        vehicle.add_component(new Typed_Producer<Counter_Data>(
            LABEL, vm_id, Component_Ports::TEST_INTEREST_PUB));
        break;
    }

    vehicle.initialize();
    vehicle.run();
    return 0;
}

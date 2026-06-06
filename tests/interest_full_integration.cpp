// Etapa 5 -- Teste 5: integracao em carga.
// vm1 RSU; vm2..vm22 sao veiculos. Cada veiculo tem 5 componentes:
// 3 produtores (SPEED, LIDAR, RADAR) e 2 consumidores.

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

const char LABEL[] = "interest-full";
const int VM_COUNT = 22;
const int RSU_VM_ID = 1;
const int FIRST_VEHICLE_VM = 2;
const int VEHICLE_COUNT = VM_COUNT - 1;
const int MIN_PRODUCERS_PER_CONSUMER = 8;
const int MIN_RESPONSES_PER_PRODUCER = 2;
const int64_t DEADLINE_NS = 240LL * 1000000000LL;

constexpr Component::Port PORT_SPEED_PUB = 0xF520;
constexpr Component::Port PORT_LIDAR_PUB = 0xF521;
constexpr Component::Port PORT_RADAR_PUB = 0xF522;
constexpr Component::Port PORT_SPEED_SUB = 0xF530;
constexpr Component::Port PORT_SECOND_SUB = 0xF531;

template <typename Data>
typename Data::Value make_value(int, uint64_t);

int producers_ready(const std::map<uint64_t, int> & by_producer, int min_samples) {
    int ready = 0;
    for (const auto & kv : by_producer)
        if (kv.second >= min_samples) ++ready;
    return ready;
}

template <>
Speed_Data::Value make_value<Speed_Data>(int vm_id, uint64_t seq) {
    return Speed_Data::Value{static_cast<double>(vm_id) + static_cast<double>(seq) / 100.0};
}

template <>
Lidar_Data::Value make_value<Lidar_Data>(int vm_id, uint64_t seq) {
    return Lidar_Data::Value{100.0 + static_cast<double>(vm_id) + static_cast<double>(seq)};
}

template <>
Radar_Data::Value make_value<Radar_Data>(int vm_id, uint64_t seq) {
    return Radar_Data::Value{200.0 + static_cast<double>(vm_id) + static_cast<double>(seq)};
}

template <typename Data>
class Fleet_Producer : public Component, public IProducer<typename Data::Value> {
public:
    Fleet_Producer(int vm_id, Port port, int start_s)
        : Component(LABEL), _vm_id(vm_id), _port_id(port), _start_s(start_s) {}

    void initialize() override {}
    Port logical_port() const override { return _port_id; }
    bool subscribe_logical_broadcast() const override { return false; }
    typename Data::Value produce() override { return make_value<Data>(_vm_id, ++_seq); }

    void run() override {
        sleep(_start_s);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] produtor unit=" << static_cast<uint32_t>(Data::UNIT)
                  << " start_s=" << _start_s << std::endl;
        SmartData<Data> data(_communicator, this);
        while (true) pause();
    }

private:
    int _vm_id;
    Port _port_id;
    int _start_s;
    uint64_t _seq = 0;
};

template <typename Data>
class Fleet_Consumer : public Component {
public:
    Fleet_Consumer(int vm_id, Port port, uint64_t period_us, int start_s,
                   int min_ready, int min_samples)
        : Component(LABEL), _vm_id(vm_id), _port_id(port),
          _period_us(period_us), _start_s(start_s),
          _min_ready(min_ready), _min_samples(min_samples) {}

    void initialize() override {}
    Port logical_port() const override { return _port_id; }
    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        sleep(_start_s);
        SmartData<Data> data(_communicator, _period_us);

        std::map<uint64_t, int> by_producer;
        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        while (Clock::now_ns() < deadline) {
            Message * m = data.receive_response(2500);
            if (!m) continue;

            if (!decode_response<Data>(m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] FAIL unit errada no consumidor" << std::endl;
                delete m;
                std::exit(1);
            }

            by_producer[endpoint_key(m->address())]++;
            delete m;

            if (producers_ready(by_producer, _min_samples) >= _min_ready) break;
        }

        const int ready = producers_ready(by_producer, _min_samples);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO unit=" << static_cast<uint32_t>(Data::UNIT)
                  << " period_us=" << _period_us
                  << " produtores=" << by_producer.size()
                  << " prontos=" << ready
                  << " minimo=" << _min_ready
                  << " amostras_min=" << _min_samples
                  << " respostas=" << data.response_count() << std::endl;

        if (ready < _min_ready) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL prontos=" << ready
                      << " minimo=" << _min_ready << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
    Port _port_id;
    uint64_t _period_us;
    int _start_s;
    int _min_ready;
    int _min_samples;
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
    const int offset = vm_id - FIRST_VEHICLE_VM;

    vehicle.add_component(new Fleet_Producer<Speed_Data>(
        vm_id, PORT_SPEED_PUB, 5 + (offset % 3)), PORT_SPEED_PUB);
    vehicle.add_component(new Fleet_Producer<Lidar_Data>(
        vm_id, PORT_LIDAR_PUB, 7 + (offset % 4)), PORT_LIDAR_PUB);
    vehicle.add_component(new Fleet_Producer<Radar_Data>(
        vm_id, PORT_RADAR_PUB, 9 + (offset % 5)), PORT_RADAR_PUB);

    vehicle.add_component(new Fleet_Consumer<Speed_Data>(
        vm_id, PORT_SPEED_SUB, 300'000, 10 + (offset % 4),
        MIN_PRODUCERS_PER_CONSUMER, MIN_RESPONSES_PER_PRODUCER), PORT_SPEED_SUB);

    if ((vm_id % 2) == 0) {
        vehicle.add_component(new Fleet_Consumer<Lidar_Data>(
            vm_id, PORT_SECOND_SUB, 600'000, 12 + (offset % 4), 2, 1), PORT_SECOND_SUB);
    } else {
        vehicle.add_component(new Fleet_Consumer<Radar_Data>(
            vm_id, PORT_SECOND_SUB, 900'000, 12 + (offset % 4), 2, 1), PORT_SECOND_SUB);
    }

    vehicle.initialize();
    vehicle.run();
    return 0;
}

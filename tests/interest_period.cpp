// Etapa 5 -- Aderencia ao periodo pedido no Interesse.
// vm1 RSU; vm2 subscriber mede o intervalo entre Respostas; vm3 publisher.
// Recepcao push: cada Resposta dispara on_response_received (registra a chegada).

#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/iproducer.h"
#include "../src/communication/smart_data/smart_data.h"
#include "../src/communication/smart_data/data_types.h"
#include "../src/core/clock.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>
#include <unistd.h>

namespace {

const int      VM_COUNT         = 3;
const int      RSU_VM_ID        = 1;
const int      SUBSCRIBER_VM_ID = 2;
const uint64_t PERIOD_US        = 250'000;
const int      SAMPLES          = 25;
const int      WARMUP           = 5;
const uint64_t PUB_ANNOUNCE_AFTER = 1;
const int      STARTUP_DELAY_S  = 5;
const int64_t  DEADLINE_NS      = 90LL * 1000000000LL;
const double   LOW_FACTOR  = 0.5;
const double   HIGH_FACTOR = 1.8;

const char LABEL[] = "interest-period";

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) { std::cerr << "[" << LABEL << "] sem /proc/cmdline" << std::endl; std::exit(1); }
    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[" << LABEL << "] falha lendo /proc/cmdline" << std::endl; std::exit(1);
    }
    std::fclose(cmdline);
    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(tok, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[" << LABEL << "] vm_id invalido: " << vm_id << std::endl; std::exit(1);
            }
            return vm_id;
        }
    }
    std::cerr << "[" << LABEL << "] so2.vm_id ausente" << std::endl; std::exit(1);
}

class Publisher_Component : public Component, public IProducer<Counter_Data::Value> {
public:
    explicit Publisher_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_PUB; }
    bool wants_raw_communicator() const override { return false; }
    Counter_Data::Value produce() override { return Counter_Data::Value{ ++_seq }; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id << "] publisher pronto" << std::endl;
        SmartData<Counter_Data> producer(_channel, this, _port);
        static std::atomic<bool> announced{false};
        const int vm_id = _vm_id;
        producer.on_response_sent([vm_id](uint64_t n) {
            if (n >= PUB_ANNOUNCE_AFTER && !announced.exchange(true))
                std::cout << "[" << LABEL << "][vm" << vm_id << "] cenario validado." << std::endl;
        });
        while (true) pause();
    }

private:
    int _vm_id;
    uint64_t _seq = 0;
};

class Subscriber_Component : public Component {
public:
    explicit Subscriber_Component(int vm_id) : Component(LABEL), _vm_id(vm_id) {}
    void initialize() override {}
    Port logical_port() const override { return Component_Ports::TEST_INTEREST_SUB; }
    bool wants_raw_communicator() const override { return false; }

    void run() override {
        sleep(STARTUP_DELAY_S);
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] subscriber period_us=" << PERIOD_US << std::endl;

        std::mutex mtx;
        std::vector<int64_t> arrivals;
        SmartData<Counter_Data> consumer(_channel, PERIOD_US, _port);
        consumer.on_response_received([&](const Counter_Data::Value &, int64_t recv_ns) {
            std::lock_guard<std::mutex> l(mtx);
            arrivals.push_back(recv_ns);
        });

        const int64_t deadline = Clock::now_ns() + DEADLINE_NS;
        for (;;) {
            { std::lock_guard<std::mutex> l(mtx); if (static_cast<int>(arrivals.size()) >= SAMPLES) break; }
            if (Clock::now_ns() >= deadline) break;
            consumer.wait_for_responses(consumer.response_count() + 1, 2000);
        }

        std::vector<int64_t> a;
        { std::lock_guard<std::mutex> l(mtx); a = arrivals; }
        if (static_cast<int>(a.size()) < SAMPLES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL amostras=" << a.size() << std::endl;
            std::exit(1);
        }

        std::vector<int64_t> deltas;
        for (std::size_t i = 1; i < a.size(); ++i) deltas.push_back((a[i] - a[i - 1]) / 1000);
        std::vector<int64_t> stable(deltas.begin() + std::min<std::size_t>(WARMUP, deltas.size()),
                                    deltas.end());
        int64_t sum = 0;
        for (int64_t d : stable) sum += d;
        int64_t avg = stable.empty() ? 0 : sum / static_cast<int64_t>(stable.size());
        int64_t mn = stable.empty() ? 0 : *std::min_element(stable.begin(), stable.end());
        int64_t mx = stable.empty() ? 0 : *std::max_element(stable.begin(), stable.end());

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] RESUMO period_pedido_us=" << PERIOD_US
                  << " avg_us=" << avg << " min_us=" << mn << " max_us=" << mx
                  << " amostras=" << stable.size() << std::endl;

        const int64_t low  = static_cast<int64_t>(LOW_FACTOR  * PERIOD_US);
        const int64_t high = static_cast<int64_t>(HIGH_FACTOR * PERIOD_US);
        if (avg < low || avg > high) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL avg_us=" << avg << " fora de [" << low << "," << high << "]" << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();
    if (vm_id == RSU_VM_ID) {
        RSU rsu; rsu.initialize(); rsu.run(); return 0;
    }
    Vehicle vehicle(false);
    if (vm_id == SUBSCRIBER_VM_ID)
        vehicle.add_component(new Subscriber_Component(vm_id), Component_Ports::TEST_INTEREST_SUB);
    else
        vehicle.add_component(new Publisher_Component(vm_id), Component_Ports::TEST_INTEREST_PUB);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

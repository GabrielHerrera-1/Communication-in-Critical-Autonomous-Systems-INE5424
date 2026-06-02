#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message/message.h"
#include "../src/core/clock.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>


namespace {

static const int VM_COUNT             = 6;
static const int RSU_VM_ID            = 1;
static const int SENDER_A_VM_ID       = 2;
static const int SENDER_B_VM_ID       = 3;
static const int STARTUP_DELAY_S      = 10;
static const int MASTER_SEND_COUNT    = 30;
static const int SLAVE_RECV_TARGET    = 25;
static const unsigned int SEND_INTERVAL_MS = 500;

static const int64_t MAX_OFFSET_US = 50'000;

static const char LABEL[] = "sptp-simple";

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[" << LABEL << "] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }
    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[" << LABEL << "] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(tok, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[" << LABEL << "] vm_id invalido: " << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }
    std::cerr << "[" << LABEL << "] parametro so2.vm_id ausente" << std::endl;
    std::exit(1);
}

class Sender : public Component {
public:
    explicit Sender(int vm_id) : Component(LABEL), _vm_id(vm_id) {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_SPTP_SIMPLE_SENDER;
    }

    bool subscribe_logical_broadcast() const override { return false; }

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        sleep(STARTUP_DELAY_S);

        if (_vm_id == SENDER_B_VM_ID) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] iniciando envio msgs=" << MASTER_SEND_COUNT << std::endl;

        for (int seq = 1; seq <= MASTER_SEND_COUNT; ++seq) {
            char payload[48];
            std::snprintf(payload, sizeof(payload), "%s:vm%d:%d", LABEL, _vm_id, seq);
            Message m(payload, std::strlen(payload) + 1);
            if (!_communicator->send(&m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] falha no envio seq=" << seq << std::endl;
                std::exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SEND_INTERVAL_MS));
        }

        for (int i = 0; i < 3; ++i) {
            char done_payload[48];
            std::snprintf(done_payload, sizeof(done_payload), "%s:vm%d:done", LABEL, _vm_id);
            Message done_m(done_payload, std::strlen(done_payload) + 1);
            _communicator->send(&done_m);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] envio concluido sent=" << MASTER_SEND_COUNT << std::endl;
        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

class Receiver : public Component {
public:
    explicit Receiver(int vm_id) : Component(LABEL), _vm_id(vm_id) {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_SPTP_SIMPLE_RECEIVER;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        std::vector<int64_t> deltas_a;
        std::vector<int64_t> deltas_b;
        deltas_a.reserve(SLAVE_RECV_TARGET);
        deltas_b.reserve(SLAVE_RECV_TARGET);

        const std::size_t target_per_sender =
            static_cast<std::size_t>(SLAVE_RECV_TARGET);

        bool sender_a_done = false;
        bool sender_b_done = false;

        auto done_with_a = [&]() {
            return sender_a_done || deltas_a.size() >= target_per_sender;
        };
        auto done_with_b = [&]() {
            return sender_b_done || deltas_b.size() >= target_per_sender;
        };

        while (!done_with_a() || !done_with_b()) {
            Message m;
            if (!_communicator->receive(&m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] falha em receive()" << std::endl;
                std::exit(1);
            }

            int64_t now_ns   = Clock::now_ns();
            int64_t msg_ns   = m.timestamp();
            int64_t delta_us = (now_ns - msg_ns) / 1000;

            const char * payload = reinterpret_cast<const char *>(m.data());

            int sender_vm = 0;
            char tail[16];
            if (std::sscanf(payload, "sptp-simple:vm%d:%15s", &sender_vm, tail) != 2) {
                continue;
            }

            if (std::strcmp(tail, "done") == 0) {
                if (sender_vm == SENDER_A_VM_ID) sender_a_done = true;
                else if (sender_vm == SENDER_B_VM_ID) sender_b_done = true;
                continue;
            }

            int seq = std::atoi(tail);
            if (seq <= 0) {
                continue;
            }

            std::vector<int64_t> * bucket = nullptr;
            if (sender_vm == SENDER_A_VM_ID && deltas_a.size() < target_per_sender) {
                bucket = &deltas_a;
            } else if (sender_vm == SENDER_B_VM_ID && deltas_b.size() < target_per_sender) {
                bucket = &deltas_b;
            }

            if (!bucket) {
                continue;
            }

            bucket->push_back(delta_us);
            std::cout << "[" << LABEL << "][vm" << _vm_id
                      << "] OFFSET from=vm" << sender_vm
                      << " seq=" << seq
                      << " delta_us=" << delta_us << std::endl;
        }

        const std::size_t MIN_SAMPLES = 10;
        if (deltas_a.size() < MIN_SAMPLES || deltas_b.size() < MIN_SAMPLES) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL amostras insuficientes: a=" << deltas_a.size()
                      << " b=" << deltas_b.size()
                      << " (minimo=" << MIN_SAMPLES << ")" << std::endl;
            std::exit(1);
        }

        auto summarize = [&](const std::vector<int64_t> & deltas, int sender_vm) -> int64_t {
            const std::size_t skip = std::min<std::size_t>(3, deltas.size());
            std::vector<int64_t> stable(deltas.begin() + skip, deltas.end());
            if (stable.empty()) return 0;

            int64_t min_us = *std::min_element(stable.begin(), stable.end());
            int64_t max_us = *std::max_element(stable.begin(), stable.end());
            int64_t sum_abs = 0;
            for (int64_t v : stable) sum_abs += (v < 0 ? -v : v);
            int64_t avg_abs = sum_abs / static_cast<int64_t>(stable.size());

            std::cout << "[" << LABEL << "][vm" << _vm_id
                      << "] RESUMO from=vm" << sender_vm
                      << " samples=" << stable.size()
                      << " min_us=" << min_us
                      << " max_us=" << max_us
                      << " avg_abs_us=" << avg_abs << std::endl;
            return avg_abs;
        };

        const int64_t avg_a = summarize(deltas_a, SENDER_A_VM_ID);
        const int64_t avg_b = summarize(deltas_b, SENDER_B_VM_ID);
        const int64_t worst = std::max(avg_a, avg_b);

        // assercao de qualidade: a precisao da sync precisa ser suficiente para
        // que (origin, timestamp) identifique inequivocamente cada mensagem
        if (worst > MAX_OFFSET_US) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] FAIL avg_abs_us=" << worst
                      << " > MAX_OFFSET_US=" << MAX_OFFSET_US << std::endl;
            std::exit(1);
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

}

int main() {
    const int vm_id = detect_vm_id();

    if (vm_id == RSU_VM_ID) {
        RSU rsu;
        rsu.initialize();
        rsu.run();
        return 0;
    }

    Vehicle vehicle(false);
    if (vm_id == SENDER_A_VM_ID || vm_id == SENDER_B_VM_ID) {
        vehicle.add_component(new Sender(vm_id),
                              Component_Ports::TEST_SPTP_SIMPLE_SENDER);
    } else {
        vehicle.add_component(new Receiver(vm_id),
                              Component_Ports::TEST_SPTP_SIMPLE_RECEIVER);
    }
    vehicle.initialize();
    vehicle.run();
    return 0;
}

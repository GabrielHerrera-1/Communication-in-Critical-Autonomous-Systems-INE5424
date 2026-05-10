#include "../src/application/rsu.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"
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

// sptp-simple: roda a pilha padrao (Communicator -> Vehicle_Protocol -> NIC ->
// engines) em 5 VMs e exercita a SPTP.
//   vm1 = RSU dedicada (master SPTP, sem componentes de aplicacao)
//   vm2 = Vehicle slave + Sender (transmite as mensagens de teste)
//   vm3-5 = Vehicle slave + Receiver (medem offset por pacote recebido)
//
// delta_us = slave_realtime_at_recv - master_realtime_at_send (em us)
//          = delay_propagacao + offset_residual_pos_sync + drift_acumulado

namespace {

static const int VM_COUNT          = 5;
static const int RSU_VM_ID         = 1;
static const int SENDER_VM_ID      = 2;
static const int STARTUP_DELAY_S   = 5;     // tempo pra slaves subirem o gateway e SPTP convergir
static const int MASTER_SEND_COUNT = 30;    // ~15s de envio
static const int SLAVE_RECV_TARGET = 25;
static const unsigned int SEND_INTERVAL_MS = 500;

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
    Sender() : Component(LABEL) {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_SPTP_SIMPLE_SENDER;
    }

    bool subscribe_logical_broadcast() const override { return false; }

    // SCHED_DEADLINE: sender envia 1 mensagem a cada 500ms, runtime de 20ms
    // por periodo (folga ~20x sobre o trabalho real ~1ms; cobre maquinas
    // significativamente mais lentas que a nossa sem risco de throttle).
    // period = deadline = 500ms casa com o ritmo natural de envio do teste.
    RT_Profile rt_profile() const override {
        RT_Profile p;
        p.policy = RT_Profile::Policy::DEADLINE;
        p.deadline = { 20'000'000ULL, 500'000'000ULL, 500'000'000ULL };
        return p;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << LABEL << "][master] communicator ausente" << std::endl;
            std::exit(1);
        }

        sleep(STARTUP_DELAY_S);

        std::cout << "[" << LABEL << "][master] iniciando envio msgs="
                  << MASTER_SEND_COUNT << std::endl;

        for (int seq = 1; seq <= MASTER_SEND_COUNT; ++seq) {
            char payload[32];
            std::snprintf(payload, sizeof(payload), "%s:%d", LABEL, seq);
            Message m(payload, std::strlen(payload) + 1);
            if (!_communicator->send(&m)) {
                std::cerr << "[" << LABEL << "][master] falha no envio seq="
                          << seq << std::endl;
                std::exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SEND_INTERVAL_MS));
        }

        std::cout << "[" << LABEL << "][master] envio concluido sent="
                  << MASTER_SEND_COUNT << std::endl;
        std::cout << "[" << LABEL << "][master] cenario validado." << std::endl;
    }
};

class Receiver : public Component {
public:
    explicit Receiver(int vm_id) : Component(LABEL), _vm_id(vm_id) {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_SPTP_SIMPLE_RECEIVER;
    }

    // SCHED_DEADLINE: receiver consome 1 mensagem a cada ~500ms (ritmo do
    // sender). period em 500ms casa. runtime 20ms da folga ~20x sobre o
    // trabalho real (sem_wait wakeup + clock_gettime + push no vector +
    // printf), cobre maquinas mais lentas sem throttle.
    RT_Profile rt_profile() const override {
        RT_Profile p;
        p.policy = RT_Profile::Policy::DEADLINE;
        p.deadline = { 20'000'000ULL, 500'000'000ULL, 500'000'000ULL };
        return p;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << LABEL << "][vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        std::vector<int64_t> deltas_us;
        deltas_us.reserve(SLAVE_RECV_TARGET);

        int received = 0;
        while (received < SLAVE_RECV_TARGET) {
            Message m;
            if (!_communicator->receive(&m)) {
                std::cerr << "[" << LABEL << "][vm" << _vm_id
                          << "] falha em receive()" << std::endl;
                std::exit(1);
            }

            // delta_ns = realtime do slave (now) - timestamp do master (no envio)
            // o timestamp foi gravado por _communicator->send via
            // Clock::monotonic_stamp(), que usa CLOCK_REALTIME. Logo delta inclui
            // delay_rede + offset_residual_pos_sync + drift_acumulado.
            int64_t now_ns   = Clock::now_ns();
            int64_t msg_ns   = m.timestamp();
            int64_t delta_us = (now_ns - msg_ns) / 1000;

            deltas_us.push_back(delta_us);
            ++received;
            std::cout << "[" << LABEL << "][vm" << _vm_id
                      << "] OFFSET seq=" << received
                      << " delta_us=" << delta_us << std::endl;
        }

        // resumo: descarta as primeiras 3 amostras (transitorio do startup).
        const std::size_t skip = std::min<std::size_t>(3, deltas_us.size());
        std::vector<int64_t> stable(deltas_us.begin() + skip, deltas_us.end());
        if (!stable.empty()) {
            int64_t min_us = *std::min_element(stable.begin(), stable.end());
            int64_t max_us = *std::max_element(stable.begin(), stable.end());
            int64_t sum_abs = 0;
            for (int64_t v : stable) sum_abs += (v < 0 ? -v : v);
            int64_t avg_abs = sum_abs / static_cast<int64_t>(stable.size());

            std::cout << "[" << LABEL << "][vm" << _vm_id
                      << "] RESUMO samples=" << stable.size()
                      << " min_us=" << min_us
                      << " max_us=" << max_us
                      << " avg_abs_us=" << avg_abs << std::endl;
        }

        std::cout << "[" << LABEL << "][vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();

    if (vm_id == RSU_VM_ID) {
        // VM dedicada como master SPTP. Nao roda Vehicle nem componentes:
        // so sobe o gateway com set_master(true) e mantem viva pra responder
        // REQUEST_SYNC dos demais. Imprime "cenario validado." pelo proprio
        // RSU::run_gateway_process pra o test runner reconhecer sucesso
        RSU rsu;
        rsu.initialize();
        rsu.run();
        return 0;
    }

    // demais VMs entram como slaves SPTP (is_master = false).
    Vehicle vehicle(false);
    if (vm_id == SENDER_VM_ID) {
        vehicle.add_component(new Sender(),
                              Component_Ports::TEST_SPTP_SIMPLE_SENDER);
    } else {
        vehicle.add_component(new Receiver(vm_id),
                              Component_Ports::TEST_SPTP_SIMPLE_RECEIVER);
    }
    vehicle.initialize();
    vehicle.run();
    return 0;
}

#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"
#include "../src/core/clock.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

// Validacao da etapa 3 (sincronizacao temporal):
//   - intra-VM: dois senders + um receiver na mesma VM. O receiver mede
//     intra_delay = recv_local_ns - send_local_ns_no_payload, que so e
//     pequeno se CLOCK_REALTIME for compartilhado entre processos da VM
//     (requisito do enunciado: "todos os processos da mesma VM tem acesso
//     ao tempo do etiquetador"). Tambem confere que (port, ts) e unico
//     entre as msgs locais (requisito de unicidade (address, timestamp)).
//   - inter-VM: master (vm1) envia, slave (vm2) calcula
//     offset = recv_local_ns - master_payload_ns
//     e exige que o |media| do offset (apos warmup) fique abaixo de
//     THRESHOLD_INTER_US apos o SPTP estabilizar.
//
// Topologia: 5 VMs. Cada VM corre 4 processos:
//   vm1: IntraSenderA, IntraSenderB, IntraReceiver, InterMaster   (master PTP)
//   vm2..vm5: IntraSenderA, IntraSenderB, IntraReceiver, InterSlave (slaves PTP)
//
// Como o broadcast logico atravessa todas as VMs, cada IntraReceiver tambem
// ve as msgs dos senders das outras 4 VMs. Para isolar a validacao intra,
// embutimos vm_id no payload e descartamos msgs cujo vm_id != local.
//
// Saida: cada componente que passa imprime "[sptp-val][<role>] cenario
// validado.". O run_qemu_test.sh exige a string "cenario validado." em
// cada log; se algum componente falha, std::exit(1) mata o vehicle inteiro
// antes da impressao.

namespace {

constexpr int VM_COUNT       = 5;
constexpr int MASTER_VM_ID   = 1;

// intra-VM
// gap de 20ms por sender + 5 VMs significa pico de 10 msgs concorrentes
// a cada 20ms (~500 msg/s agregado). pool de 50 buffers RX por NIC absorve
// folgadamente. baixar o gap pode estourar buffer com 5 VMs.
constexpr int INTRA_PER_SENDER       = 20;
constexpr int INTRA_EXPECTED_LOCAL   = 2 * INTRA_PER_SENDER;          // 2 senders locais
constexpr int INTRA_EXPECTED_TOTAL   = INTRA_EXPECTED_LOCAL * VM_COUNT; // todos os broadcasts (info)
// Em QEMU sem KVM, com 5 VMs disputando CPU, jitter de scheduling pode dar
// picos esporadicos de ate ~10ms numa msg. Usamos limite de 20ms no max e
// 5ms na media; assim o teste tolera 1-2 outliers mas falha se a media
// distorcer (sinal de que o relogio nao esta compartilhado).
constexpr int64_t THRESHOLD_INTRA_MAX_US = 20000; // 20ms para outliers
constexpr int64_t THRESHOLD_INTRA_AVG_US = 5000;  // 5ms para a media
constexpr unsigned INTRA_SEND_GAP_US = 20000;  // 20ms entre sends por sender
constexpr unsigned INTRA_STARTUP_S   = 5;      // espera todas as 5 VMs subirem

// inter-VM
constexpr int INTER_COUNT             = 30;
constexpr int INTER_WARMUP            = 5;     // descarta as primeiras N (sptp pode nao ter aplicado)
constexpr int64_t THRESHOLD_INTER_US  = 15000; // 15ms (5 slaves competindo por sync, sem KVM)
constexpr unsigned INTER_SEND_GAP_MS  = 200;
constexpr unsigned INTER_STARTUP_S    = 10;    // espera o sptp completar pelo menos 1 sync em todos os slaves

struct IntraPayload {
    uint8_t  vm_id;
    uint8_t  sender_id;   // 'A' ou 'B'
    uint16_t seq;
    int64_t  send_ns;
} __attribute__((packed));

struct InterPayload {
    uint16_t seq;
    int64_t  send_ns;
} __attribute__((packed));

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[sptp-val] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }
    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(tok, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[sptp-val] vm_id invalido: " << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }
    std::cerr << "[sptp-val] so2.vm_id ausente no cmdline" << std::endl;
    std::exit(1);
}

class Intra_Sender : public Component {
public:
    Intra_Sender(uint8_t sender_id, int vm_id)
      : Component(std::string("intra-sender-") + static_cast<char>(sender_id)),
        _sender_id(sender_id), _vm_id(vm_id) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) std::exit(1);
        sleep(INTRA_STARTUP_S);

        for (int i = 0; i < INTRA_PER_SENDER; ++i) {
            IntraPayload p{};
            p.vm_id     = static_cast<uint8_t>(_vm_id);
            p.sender_id = _sender_id;
            p.seq       = static_cast<uint16_t>(i);
            p.send_ns   = Clock::now_ns();

            Message m(&p, sizeof(p));
            if (!_communicator->send(&m)) {
                std::cerr << "[sptp-val][sender-" << static_cast<char>(_sender_id)
                          << "] falha ao enviar seq " << i << std::endl;
                std::exit(1);
            }
            usleep(INTRA_SEND_GAP_US);
        }

        std::cout << "[sptp-val][sender-" << static_cast<char>(_sender_id)
                  << "] enviou " << INTRA_PER_SENDER << " msgs locais." << std::endl;
    }

    bool subscribe_logical_broadcast() const override { return false; }
    Port logical_port() const override {
        return _sender_id == 'A'
             ? Component_Ports::TEST_SPTP_VAL_INTRA_SENDER_A
             : Component_Ports::TEST_SPTP_VAL_INTRA_SENDER_B;
    }

private:
    uint8_t _sender_id;
    int     _vm_id;
};

class Intra_Receiver : public Component {
public:
    explicit Intra_Receiver(int vm_id)
      : Component("intra-receiver"), _vm_id(vm_id) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) std::exit(1);

        // chave de unicidade (port, header_ts) para detectar duplicatas locais
        struct Key {
            uint16_t port;
            int64_t  ts;
            bool operator==(const Key & o) const { return port == o.port && ts == o.ts; }
        };
        struct KeyHash {
            std::size_t operator()(const Key & k) const noexcept {
                return std::hash<uint64_t>{}((static_cast<uint64_t>(k.port) << 48) ^
                                             static_cast<uint64_t>(k.ts));
            }
        };
        std::unordered_set<Key, KeyHash> seen;

        std::vector<int64_t> intra_delays_us;
        intra_delays_us.reserve(INTRA_EXPECTED_LOCAL);

        int local_count  = 0;
        int remote_count = 0;
        int total        = 0;

        // Timeout de seguranca: 5 VMs * 40 msgs * 20ms gap = ~3.2s ideal.
        // 60s folga generosa para QEMU sem KVM.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

        while (local_count < INTRA_EXPECTED_LOCAL &&
               std::chrono::steady_clock::now() < deadline) {
            Message m;
            if (!_communicator->receive(&m)) {
                std::cerr << "[sptp-val][intra-rx] receive falhou" << std::endl;
                std::exit(1);
            }
            ++total;

            int64_t recv_local_ns = Clock::now_ns();

            if (m.size() != sizeof(IntraPayload)) {
                // outras msgs (tipo o sptp interno nao deveria chegar aqui, mas se chegar, ignora)
                continue;
            }
            IntraPayload p;
            std::memcpy(&p, m.data(), sizeof(p));

            // filtra msgs da outra VM (broadcast atravessa)
            if (p.vm_id != static_cast<uint8_t>(_vm_id)) {
                ++remote_count;
                continue;
            }

            // unicidade local: (port_origem, ts_header) deve ser unico
            Key k{ m.origin().port, m.timestamp() };
            if (!seen.insert(k).second) {
                std::cerr << "[sptp-val][intra-rx] duplicata: port=0x"
                          << std::hex << k.port << std::dec
                          << " ts=" << k.ts << std::endl;
                std::exit(1);
            }

            int64_t delay_ns = recv_local_ns - p.send_ns;
            if (delay_ns < 0) {
                std::cerr << "[sptp-val][intra-rx] delay negativo (recv<send)? "
                          << "recv=" << recv_local_ns << " send=" << p.send_ns
                          << " — relogio nao compartilhado dentro da VM"
                          << std::endl;
                std::exit(1);
            }
            intra_delays_us.push_back(delay_ns / 1000);
            ++local_count;
        }

        if (local_count < INTRA_EXPECTED_LOCAL) {
            std::cerr << "[sptp-val][intra-rx] FALHOU: recebi apenas "
                      << local_count << "/" << INTRA_EXPECTED_LOCAL
                      << " msgs locais (total=" << total
                      << " remotas=" << remote_count << ")" << std::endl;
            std::exit(1);
        }

        // estatisticas
        int64_t sum = 0;
        int64_t mx  = 0;
        int64_t mn  = INT64_MAX;
        for (int64_t v : intra_delays_us) {
            sum += v;
            if (v > mx) mx = v;
            if (v < mn) mn = v;
        }
        int64_t avg = sum / static_cast<int64_t>(intra_delays_us.size());

        std::cout << "[sptp-val][intra-rx] amostras=" << intra_delays_us.size()
                  << " delay_us min=" << mn
                  << " max=" << mx
                  << " avg=" << avg
                  << " (limites max=" << THRESHOLD_INTRA_MAX_US
                  << " avg=" << THRESHOLD_INTRA_AVG_US << ")"
                  << " remotas_descartadas=" << remote_count
                  << std::endl;

        if (mx > THRESHOLD_INTRA_MAX_US) {
            std::cerr << "[sptp-val][intra-rx] FALHOU: delay maximo "
                      << mx << "us excede limite " << THRESHOLD_INTRA_MAX_US << "us"
                      << std::endl;
            std::exit(1);
        }
        if (avg > THRESHOLD_INTRA_AVG_US) {
            std::cerr << "[sptp-val][intra-rx] FALHOU: delay medio "
                      << avg << "us excede limite " << THRESHOLD_INTRA_AVG_US << "us"
                      << std::endl;
            std::exit(1);
        }

        std::cout << "[sptp-val][intra] cenario validado." << std::endl;
    }

    Port logical_port() const override {
        return Component_Ports::TEST_SPTP_VAL_INTRA_RECEIVER;
    }

private:
    int _vm_id;
};

class Inter_Master : public Component {
public:
    Inter_Master() : Component("inter-master") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) std::exit(1);

        sleep(INTER_STARTUP_S);

        for (int i = 0; i < INTER_COUNT; ++i) {
            InterPayload p{};
            p.seq     = static_cast<uint16_t>(i);
            p.send_ns = Clock::now_ns();

            Message m(&p, sizeof(p));
            if (!_communicator->send(&m)) {
                std::cerr << "[sptp-val][inter-master] falha ao enviar seq " << i << std::endl;
                std::exit(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(INTER_SEND_GAP_MS));
        }

        std::cout << "[sptp-val][inter-master] enviou " << INTER_COUNT
                  << " msgs. cenario validado." << std::endl;
    }

    bool subscribe_logical_broadcast() const override { return false; }
    Port logical_port() const override { return Component_Ports::TEST_SPTP_VAL_INTER_MASTER; }
};

class Inter_Slave : public Component {
public:
    Inter_Slave() : Component("inter-slave") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) std::exit(1);

        std::vector<int64_t> offsets_us;
        offsets_us.reserve(INTER_COUNT);

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(60);
        int from_master = 0;

        while (from_master < INTER_COUNT &&
               std::chrono::steady_clock::now() < deadline) {
            Message m;
            if (!_communicator->receive(&m)) {
                std::cerr << "[sptp-val][inter-slave] receive falhou" << std::endl;
                std::exit(1);
            }
            int64_t recv_local_ns = Clock::now_ns();

            // so processamos msgs vindas do master inter (descarta intras de qq vm)
            if (m.origin().port != Component_Ports::TEST_SPTP_VAL_INTER_MASTER) continue;
            if (m.size() != sizeof(InterPayload)) continue;

            InterPayload p;
            std::memcpy(&p, m.data(), sizeof(p));

            ++from_master;
            if (from_master <= INTER_WARMUP) continue;  // sptp pode estar mid-flight

            int64_t offset_us = (recv_local_ns - p.send_ns) / 1000;
            offsets_us.push_back(offset_us);
        }

        if (from_master < INTER_COUNT) {
            std::cerr << "[sptp-val][inter-slave] FALHOU: recebi apenas "
                      << from_master << "/" << INTER_COUNT
                      << " msgs do master" << std::endl;
            std::exit(1);
        }

        if (offsets_us.empty()) {
            std::cerr << "[sptp-val][inter-slave] FALHOU: 0 amostras pos-warmup"
                      << std::endl;
            std::exit(1);
        }

        int64_t sum = 0;
        int64_t mx  = INT64_MIN;
        int64_t mn  = INT64_MAX;
        for (int64_t v : offsets_us) {
            sum += v;
            if (v > mx) mx = v;
            if (v < mn) mn = v;
        }
        int64_t avg = sum / static_cast<int64_t>(offsets_us.size());
        int64_t abs_avg = avg < 0 ? -avg : avg;

        std::cout << "[sptp-val][inter-slave] amostras=" << offsets_us.size()
                  << " offset_us min=" << mn
                  << " max=" << mx
                  << " avg=" << avg
                  << " (|avg|=" << abs_avg
                  << ", limite=" << THRESHOLD_INTER_US << ")"
                  << std::endl;

        if (abs_avg > THRESHOLD_INTER_US) {
            std::cerr << "[sptp-val][inter-slave] FALHOU: |offset medio| "
                      << abs_avg << "us excede limite "
                      << THRESHOLD_INTER_US << "us — sptp nao convergiu"
                      << std::endl;
            std::exit(1);
        }

        std::cout << "[sptp-val][inter-slave] cenario validado." << std::endl;
    }

    Port logical_port() const override { return Component_Ports::TEST_SPTP_VAL_INTER_SLAVE; }
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();

    Vehicle vehicle;
    vehicle.add_component(new Intra_Sender('A', vm_id),
                          Component_Ports::TEST_SPTP_VAL_INTRA_SENDER_A);
    vehicle.add_component(new Intra_Sender('B', vm_id),
                          Component_Ports::TEST_SPTP_VAL_INTRA_SENDER_B);
    vehicle.add_component(new Intra_Receiver(vm_id),
                          Component_Ports::TEST_SPTP_VAL_INTRA_RECEIVER);

    if (vm_id == MASTER_VM_ID) {
        vehicle.add_component(new Inter_Master(),
                              Component_Ports::TEST_SPTP_VAL_INTER_MASTER);
    } else {
        vehicle.add_component(new Inter_Slave(),
                              Component_Ports::TEST_SPTP_VAL_INTER_SLAVE);
    }

    vehicle.initialize();
    vehicle.run();
    return 0;
}

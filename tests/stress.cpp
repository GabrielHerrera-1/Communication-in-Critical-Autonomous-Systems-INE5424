#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <unistd.h>
#include <vector>

namespace {

#ifndef STRESS_MESSAGE_COUNT
#define STRESS_MESSAGE_COUNT 200
#endif

#ifndef STRESS_INTERSTICE_USEC
#define STRESS_INTERSTICE_USEC 500
#endif

#ifndef STRESS_DONE_REPEATS
#define STRESS_DONE_REPEATS 200
#endif

#ifndef STRESS_DONE_INTERSTICE_USEC
#define STRESS_DONE_INTERSTICE_USEC 10000
#endif

#ifndef STRESS_DONE_EXIT_THRESHOLD
#define STRESS_DONE_EXIT_THRESHOLD (STRESS_DONE_REPEATS / 3)
#endif

#ifndef STRESS_STARTUP_DELAY_SEC
#define STRESS_STARTUP_DELAY_SEC 10
#endif

#ifndef STRESS_VM_STAGGER_USEC
#define STRESS_VM_STAGGER_USEC 250000
#endif

#ifndef STRESS_POST_DATA_DRAIN_USEC
#define STRESS_POST_DATA_DRAIN_USEC 2000000
#endif

#ifndef STRESS_RX_HARD_TIMEOUT_SEC
#define STRESS_RX_HARD_TIMEOUT_SEC 180
#endif

#ifndef STRESS_SEND_RETRY_LIMIT
#define STRESS_SEND_RETRY_LIMIT 50
#endif

#ifndef STRESS_SEND_RETRY_USEC
#define STRESS_SEND_RETRY_USEC 200
#endif

static const int VM_COUNT = 5;

static const int MESSAGE_COUNT = STRESS_MESSAGE_COUNT;
static const int DONE_REPEATS = STRESS_DONE_REPEATS;
static const unsigned int INTERSTICE_USEC = STRESS_INTERSTICE_USEC;
static const unsigned int DONE_INTERSTICE_USEC = STRESS_DONE_INTERSTICE_USEC;
static const unsigned int STARTUP_DELAY_SEC = STRESS_STARTUP_DELAY_SEC;
static const unsigned int VM_STAGGER_USEC = STRESS_VM_STAGGER_USEC;
static const unsigned int POST_DATA_DRAIN_USEC = STRESS_POST_DATA_DRAIN_USEC;
static const unsigned int RX_HARD_TIMEOUT_SEC = STRESS_RX_HARD_TIMEOUT_SEC;
static const int SEND_RETRY_LIMIT = STRESS_SEND_RETRY_LIMIT;
static const unsigned int SEND_RETRY_USEC = STRESS_SEND_RETRY_USEC;

// Listener sai apos receber esse numero de DONEs de cada origem. Deixamos o
// limiar configuravel para separar "perda real" de "mensagem ainda em transito"
// no caminho inter-VM.
static const int DONE_GRACE_THRESHOLD = STRESS_DONE_EXIT_THRESHOLD;

static const char STRESS_LABEL[] = "stress";

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[stress] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }

    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[stress] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * token = std::strtok(line, " "); token; token = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(token, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[stress] vm_id invalido: " << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[stress] parametro so2.vm_id ausente" << std::endl;
    std::exit(1);
}

unsigned long long monotonic_us() {
    timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        std::perror("[stress] clock_gettime");
        std::exit(1);
    }
    return static_cast<unsigned long long>(ts.tv_sec) * 1000000ULL +
           static_cast<unsigned long long>(ts.tv_nsec / 1000ULL);
}

bool parse_payload(const char * payload,
                   char * kind, std::size_t kind_size,
                   int * src_vm, int * sequence) {
    char parsed_label[16];
    char parsed_kind[16];
    int parsed_src = 0;
    int parsed_sequence = 0;

    if (std::sscanf(payload, "%15[^:]:%15[^:]:%d:%d",
                    parsed_label, parsed_kind, &parsed_src, &parsed_sequence) != 4) {
        return false;
    }
    if (std::strcmp(parsed_label, STRESS_LABEL) != 0) {
        return false;
    }
    if (kind && kind_size > 0) {
        std::snprintf(kind, kind_size, "%s", parsed_kind);
    }
    if (src_vm) *src_vm = parsed_src;
    if (sequence) *sequence = parsed_sequence;
    return true;
}

// ----- Sender: single-thread, so envia. Nao chama receive(); o engine
// worker interno drena o ring SHM em segundo plano (comportamento nativo
// do SharedMemoryEngine, nao e thread de aplicacao).
class Stress_Sender_Component : public Component {
public:
    explicit Stress_Sender_Component(int vm_id)
        : Component("stress-sender"), _vm_id(vm_id) {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_STRESS_SENDER;
    }

    bool subscribe_logical_broadcast() const override {
        return false;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[stress][sender-vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        sleep(STARTUP_DELAY_SEC);

        if (VM_STAGGER_USEC > 0 && _vm_id > 1) {
            usleep(static_cast<useconds_t>(VM_STAGGER_USEC * static_cast<unsigned int>(_vm_id - 1)));
        }

        std::cout << "[stress][sender-vm" << _vm_id
                  << "] iniciando envio msgs=" << MESSAGE_COUNT
                  << " interstice_us=" << INTERSTICE_USEC
                  << " vm_stagger_us=" << VM_STAGGER_USEC
                  << " total_on_wire=" << (MESSAGE_COUNT * VM_COUNT) << std::endl;

        const unsigned long long start_us = monotonic_us();
        char payload[64];

        int local_send_drops = 0;
        for (int seq = 1; seq <= MESSAGE_COUNT; ++seq) {
            std::snprintf(payload, sizeof(payload), "%s:DATA:%d:%d",
                          STRESS_LABEL, _vm_id, seq);
            Message msg(payload, std::strlen(payload) + 1);
            if (!send_with_retry(&msg)) {
                ++local_send_drops;
            }
            if (INTERSTICE_USEC > 0 && seq != MESSAGE_COUNT) {
                usleep(INTERSTICE_USEC);
            }
        }

        const unsigned long long data_end_us = monotonic_us();

        // Deixa o trafego pesado baixar antes de emitir as sentinelas, para
        // maximizar a chance de ao menos uma copia sobreviver em cada peer.
        usleep(POST_DATA_DRAIN_USEC);

        int local_done_drops = 0;
        for (int i = 1; i <= DONE_REPEATS; ++i) {
            std::snprintf(payload, sizeof(payload), "%s:DONE:%d:%d",
                          STRESS_LABEL, _vm_id, i);
            Message msg(payload, std::strlen(payload) + 1);
            if (!send_with_retry(&msg)) {
                ++local_done_drops;
            }
            usleep(DONE_INTERSTICE_USEC);
        }

        const unsigned long long elapsed_us = data_end_us - start_us;
        const double avg_send_us =
            static_cast<double>(elapsed_us) / static_cast<double>(MESSAGE_COUNT);

        std::cout << "[stress][sender-vm" << _vm_id
                  << "] SEND_DONE sent_data=" << MESSAGE_COUNT
                  << " sent_done=" << DONE_REPEATS
                  << " data_elapsed_us=" << elapsed_us
                  << " avg_send_us=" << avg_send_us
                  << " local_send_drops=" << local_send_drops
                  << " local_done_drops=" << local_done_drops
                  << std::endl;
        std::cout << "[stress][sender-vm" << _vm_id
                  << "] envio concluido." << std::endl;
    }

private:
    bool send_with_retry(Message * msg) {
        for (int attempt = 0; attempt < SEND_RETRY_LIMIT; ++attempt) {
            if (_communicator->send(msg)) {
                return true;
            }
            usleep(SEND_RETRY_USEC);
        }
        return false;
    }

    int _vm_id;
};

// ----- Listener: single-thread. So chama receive() em loop, contabilizando
// por origem (intra quando src == vm_id local, inter caso contrario).
class Stress_Listener_Component : public Component {
public:
    explicit Stress_Listener_Component(int vm_id)
        : Component("stress-listener"), _vm_id(vm_id) {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_STRESS_LISTENER;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[stress][listener-vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        std::vector<std::vector<unsigned char>> seen(VM_COUNT + 1);
        for (int v = 1; v <= VM_COUNT; ++v) {
            seen[v].assign(MESSAGE_COUNT + 1, 0);
        }
        std::vector<int> unique_count(VM_COUNT + 1, 0);
        std::vector<int> duplicates(VM_COUNT + 1, 0);
        std::vector<int> done_count(VM_COUNT + 1, 0);
        int unrelated = 0;
        int out_of_range = 0;

        // Sources = todas as VMs, inclusive a propria (testamos intra-VM).
        int sources_done = 0;
        const int expected_sources = VM_COUNT;

        const unsigned long long deadline_us =
            monotonic_us() +
            static_cast<unsigned long long>(RX_HARD_TIMEOUT_SEC) * 1000000ULL;

        while (sources_done < expected_sources) {
            if (monotonic_us() >= deadline_us) {
                std::cout << "[stress][listener-vm" << _vm_id
                          << "] timeout aguardando DONE dos peers" << std::endl;
                break;
            }

            Message msg;
            if (!_communicator->receive(&msg)) {
                std::cerr << "[stress][listener-vm" << _vm_id
                          << "] falha em receive()" << std::endl;
                std::exit(1);
            }

            const char * payload = reinterpret_cast<const char *>(msg.data());
            char kind[16];
            int src = 0;
            int seq = 0;
            if (!parse_payload(payload, kind, sizeof(kind), &src, &seq)) {
                ++unrelated;
                continue;
            }
            if (src < 1 || src > VM_COUNT) {
                ++out_of_range;
                continue;
            }

            if (std::strcmp(kind, "DATA") == 0) {
                if (seq < 1 || seq > MESSAGE_COUNT) {
                    ++out_of_range;
                    continue;
                }
                if (seen[src][seq]) {
                    ++duplicates[src];
                } else {
                    seen[src][seq] = 1;
                    ++unique_count[src];
                }
            } else if (std::strcmp(kind, "DONE") == 0) {
                const int previous = done_count[src];
                ++done_count[src];
                if (previous < DONE_GRACE_THRESHOLD &&
                    done_count[src] >= DONE_GRACE_THRESHOLD) {
                    ++sources_done;
                }
            } else {
                ++unrelated;
            }
        }

        // Relatorio por origem, separando intra-VM de inter-VM.
        int total_expected_inter = 0;
        int total_unique_inter = 0;
        int total_dupes_inter = 0;
        int intra_unique = 0;

        for (int src = 1; src <= VM_COUNT; ++src) {
            const int lost = MESSAGE_COUNT - unique_count[src];
            const double loss_pct =
                (100.0 * static_cast<double>(lost)) / static_cast<double>(MESSAGE_COUNT);

            int gap_runs = 0;
            int max_gap = 0;
            int current_gap = 0;
            for (int s = 1; s <= MESSAGE_COUNT; ++s) {
                if (!seen[src][s]) {
                    ++current_gap;
                    if (current_gap > max_gap) max_gap = current_gap;
                } else {
                    if (current_gap > 0) ++gap_runs;
                    current_gap = 0;
                }
            }
            if (current_gap > 0) ++gap_runs;

            const char * kind_label = (src == _vm_id) ? "intra" : "inter";

            std::cout << "[stress][listener-vm" << _vm_id
                      << "] PAIR kind=" << kind_label
                      << " src=vm" << src
                      << " expected=" << MESSAGE_COUNT
                      << " unique=" << unique_count[src]
                      << " lost=" << lost
                      << " loss_pct=" << loss_pct
                      << " dupes=" << duplicates[src]
                      << " gap_runs=" << gap_runs
                      << " max_gap=" << max_gap
                      << " done_rcvd=" << done_count[src]
                      << std::endl;

            if (src == _vm_id) {
                intra_unique = unique_count[src];
            } else {
                total_expected_inter += MESSAGE_COUNT;
                total_unique_inter += unique_count[src];
                total_dupes_inter += duplicates[src];
            }
        }

        const int intra_lost = MESSAGE_COUNT - intra_unique;
        const double intra_loss_pct =
            (100.0 * static_cast<double>(intra_lost)) / static_cast<double>(MESSAGE_COUNT);
        const int total_lost_inter = total_expected_inter - total_unique_inter;
        const double total_loss_pct_inter = total_expected_inter > 0
            ? (100.0 * static_cast<double>(total_lost_inter) / static_cast<double>(total_expected_inter))
            : 0.0;

        std::cout << "[stress][listener-vm" << _vm_id
                  << "] RESUMO"
                  << " intra_expected=" << MESSAGE_COUNT
                  << " intra_unique=" << intra_unique
                  << " intra_lost=" << intra_lost
                  << " intra_loss_pct=" << intra_loss_pct
                  << " inter_expected=" << total_expected_inter
                  << " inter_unique=" << total_unique_inter
                  << " inter_lost=" << total_lost_inter
                  << " inter_loss_pct=" << total_loss_pct_inter
                  << " dupes_inter=" << total_dupes_inter
                  << " unrelated=" << unrelated
                  << " out_of_range=" << out_of_range
                  << std::endl;
        std::cout << "[stress][listener-vm" << _vm_id
                  << "] cenario validado." << std::endl;
    }

private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();

    Vehicle vehicle;
    vehicle.add_component(new Stress_Sender_Component(vm_id),
                          Component_Ports::TEST_STRESS_SENDER);
    vehicle.add_component(new Stress_Listener_Component(vm_id),
                          Component_Ports::TEST_STRESS_LISTENER);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

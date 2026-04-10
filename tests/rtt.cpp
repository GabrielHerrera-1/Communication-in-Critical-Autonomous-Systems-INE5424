#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <limits>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

struct RTT_Config {
    const char * label;
    int sample_count;
    int background_messages_per_vm;
    unsigned int startup_delay_sec;
    unsigned int inter_sample_delay_usec;
    unsigned int background_message_delay_usec;
};

static const int VM_COUNT = 5;
static const int INITIATOR_VM_ID = 1;
static const int RESPONDER_VM_ID = 2;
static const int FIRST_BACKGROUND_VM_ID = 3;

bool is_background_vm(int vm_id) {
    return vm_id >= FIRST_BACKGROUND_VM_ID && vm_id <= VM_COUNT;
}

int background_vm_count() {
    return VM_COUNT - FIRST_BACKGROUND_VM_ID + 1;
}

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[rtt-benchmark] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }

    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[rtt-benchmark] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * token = std::strtok(line, " "); token; token = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(token, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[rtt-benchmark] vm_id invalido no cmdline: "
                          << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[rtt-benchmark] parametro so2.vm_id ausente no cmdline" << std::endl;
    std::exit(1);
}

void build_payload(
    char * buffer,
    std::size_t size,
    const RTT_Config & config,
    const char * kind,
    int sender_id,
    int sequence
) {
    std::snprintf(buffer, size, "%s:%s:vm%d:%d", config.label, kind, sender_id, sequence);
}

bool parse_payload(
    const char * payload,
    const RTT_Config & config,
    char * kind,
    std::size_t kind_size,
    int * sender_id,
    int * sequence
) {
    char parsed_label[64];
    char parsed_kind[16];
    int parsed_sender_id = 0;
    int parsed_sequence = 0;

    if (std::sscanf(
            payload,
            "%63[^:]:%15[^:]:vm%d:%d",
            parsed_label,
            parsed_kind,
            &parsed_sender_id,
            &parsed_sequence
        ) != 4) {
        return false;
    }

    if (std::strcmp(parsed_label, config.label) != 0) {
        return false;
    }

    if (std::strcmp(parsed_kind, "ping") != 0 && std::strcmp(parsed_kind, "pong") != 0) {
        if (std::strcmp(parsed_kind, "bg") != 0) {
            return false;
        }
    }

    if (kind && kind_size > 0) {
        std::snprintf(kind, kind_size, "%s", parsed_kind);
    }

    if (sender_id) {
        *sender_id = parsed_sender_id;
    }

    if (sequence) {
        *sequence = parsed_sequence;
    }

    return true;
}

unsigned long long monotonic_raw_us() {
    timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        std::perror("[rtt-benchmark] clock_gettime");
        std::exit(1);
    }

    return static_cast<unsigned long long>(ts.tv_sec) * 1000000ULL +
           static_cast<unsigned long long>(ts.tv_nsec / 1000ULL);
}

class RTT_Component : public Component {
    struct Parsed_Message {
        char kind[16];
        int sender_id;
        int sequence;
    };

public:
    explicit RTT_Component(const RTT_Config & config)
        : Component("rtt-gateway"),
          _config(config),
          _vm_id(detect_vm_id()),
          _matched_send_count(0),
          _matched_receive_count(0),
          _background_send_count(0),
          _background_receive_count(0) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << _config.label << "][vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        if (_config.startup_delay_sec > 0) {
            sleep(_config.startup_delay_sec);
        }

        if (_vm_id == INITIATOR_VM_ID) {
            run_initiator();
        } else if (_vm_id == RESPONDER_VM_ID) {
            run_responder();
        } else if (is_background_vm(_vm_id)) {
            run_background_peer();
        } else {
            std::cerr << "[" << _config.label << "] vm_id inesperado: "
                      << _vm_id << std::endl;
            std::exit(1);
        }

        dump_results();
        std::cout << "[" << _config.label << "][vm" << _vm_id
                  << "] RTT benchmark concluido." << std::endl;
    }

private:
    void send_message(const char * kind, int sequence) {
        char payload[96];
        build_payload(payload, sizeof(payload), _config, kind, _vm_id, sequence);

        Message message(payload, std::strlen(payload) + 1);
        if (!_communicator->send(&message)) {
            std::cerr << "[" << _config.label << "][vm" << _vm_id
                      << "] falha ao enviar " << payload << std::endl;
            std::exit(1);
        }

        if (std::strcmp(kind, "bg") == 0) {
            ++_background_send_count;
        } else {
            ++_matched_send_count;
        }
    }

    Parsed_Message receive_message() {
        Message message;
        if (!_communicator->receive(&message)) {
            std::cerr << "[" << _config.label << "][vm" << _vm_id
                      << "] falha ao receber mensagem RTT" << std::endl;
            std::exit(1);
        }

        const char * payload =
            reinterpret_cast<const char *>(message.data());

        char kind[16];
        int sender_id = 0;
        int sequence = 0;
        if (!parse_payload(payload, _config, kind, sizeof(kind), &sender_id, &sequence)) {
            std::cerr << "[" << _config.label << "][vm" << _vm_id
                      << "] payload RTT invalido: " << payload << std::endl;
            std::exit(1);
        }

        Parsed_Message parsed = {};
        std::snprintf(parsed.kind, sizeof(parsed.kind), "%s", kind);
        parsed.sender_id = sender_id;
        parsed.sequence = sequence;
        return parsed;
    }

    void receive_expected(
        const char * expected_kind,
        int expected_sender_id,
        int expected_sequence
    ) {
        while (true) {
            Parsed_Message message = receive_message();

            if (std::strcmp(message.kind, "bg") == 0) {
                if (!is_background_vm(message.sender_id) ||
                    message.sender_id == _vm_id ||
                    message.sequence < 1 ||
                    message.sequence > _config.background_messages_per_vm) {
                    std::cerr << "[" << _config.label << "][vm" << _vm_id
                              << "] mensagem de fundo invalida" << std::endl;
                    std::exit(1);
                }
                ++_background_receive_count;
                continue;
            }

            if (std::strcmp(message.kind, expected_kind) != 0 ||
                message.sender_id != expected_sender_id ||
                message.sequence != expected_sequence) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] mensagem RTT inesperada" << std::endl;
                std::exit(1);
            }

            ++_matched_receive_count;
            return;
        }
    }

    void receive_background_traffic() {
        const int expected_total =
            _config.sample_count + // pings do iniciador
            _config.sample_count + // pongs do responder
            (background_vm_count() - 1) * _config.background_messages_per_vm;

        while (_background_receive_count < expected_total) {
            Parsed_Message message = receive_message();

            if (message.sender_id == _vm_id) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] recebeu mensagem propria" << std::endl;
                std::exit(1);
            }

            if (std::strcmp(message.kind, "ping") == 0) {
                if (message.sender_id != INITIATOR_VM_ID ||
                    message.sequence < 1 ||
                    message.sequence > _config.sample_count) {
                    std::cerr << "[" << _config.label << "][vm" << _vm_id
                              << "] ping invalido durante carga" << std::endl;
                    std::exit(1);
                }
            } else if (std::strcmp(message.kind, "pong") == 0) {
                if (message.sender_id != RESPONDER_VM_ID ||
                    message.sequence < 1 ||
                    message.sequence > _config.sample_count) {
                    std::cerr << "[" << _config.label << "][vm" << _vm_id
                              << "] pong invalido durante carga" << std::endl;
                    std::exit(1);
                }
            } else if (std::strcmp(message.kind, "bg") == 0) {
                if (!is_background_vm(message.sender_id) ||
                    message.sequence < 1 ||
                    message.sequence > _config.background_messages_per_vm) {
                    std::cerr << "[" << _config.label << "][vm" << _vm_id
                              << "] mensagem de fundo invalida" << std::endl;
                    std::exit(1);
                }
            } else {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] tipo de mensagem inesperado durante carga" << std::endl;
                std::exit(1);
            }

            ++_background_receive_count;
        }
    }

    void send_background_messages() {
        for (int sequence = 1; sequence <= _config.background_messages_per_vm; ++sequence) {
            send_message("bg", sequence);

            if (_config.background_message_delay_usec > 0 &&
                sequence != _config.background_messages_per_vm) {
                usleep(_config.background_message_delay_usec);
            }
        }
    }

    void dump_results() const {
        std::cout << "[" << _config.label << "][vm" << _vm_id
                  << "] RESUMO matched_send=" << _matched_send_count
                  << " matched_receive=" << _matched_receive_count
                  << " bg_send=" << _background_send_count
                  << " bg_receive=" << _background_receive_count << std::endl;

        if (_vm_id != INITIATOR_VM_ID) {
            return;
        }

        if (_samples_us.empty()) {
            std::cerr << "[" << _config.label << "][vm" << _vm_id
                      << "] nenhuma amostra RTT coletada" << std::endl;
            std::exit(1);
        }

        for (std::size_t i = 0; i < _samples_us.size(); ++i) {
            std::cout << "[" << _config.label << "][vm" << _vm_id
                      << "] RTT_SAMPLE seq=" << (i + 1)
                      << " us=" << _samples_us[i] << std::endl;
        }

        const unsigned long long min_us =
            *std::min_element(_samples_us.begin(), _samples_us.end());
        const unsigned long long max_us =
            *std::max_element(_samples_us.begin(), _samples_us.end());

        unsigned long long total_us = 0;
        for (unsigned long long sample : _samples_us) {
            total_us += sample;
        }

        const double avg_us =
            static_cast<double>(total_us) / static_cast<double>(_samples_us.size());

        std::cout << "[" << _config.label << "][vm" << _vm_id
                  << "] RTT_RESULT samples=" << _samples_us.size()
                  << " avg_us=" << avg_us
                  << " min_us=" << min_us
                  << " max_us=" << max_us << std::endl;
    }

    void run_initiator() {
        _samples_us.reserve(_config.sample_count);

        for (int sequence = 1; sequence <= _config.sample_count; ++sequence) {
            const unsigned long long start_us = monotonic_raw_us();
            send_message("ping", sequence);
            receive_expected("pong", RESPONDER_VM_ID, sequence);
            const unsigned long long end_us = monotonic_raw_us();

            _samples_us.push_back(end_us - start_us);

            if (_config.inter_sample_delay_usec > 0 &&
                sequence != _config.sample_count) {
                usleep(_config.inter_sample_delay_usec);
            }
        }
    }

    void run_responder() {
        for (int sequence = 1; sequence <= _config.sample_count; ++sequence) {
            receive_expected("ping", INITIATOR_VM_ID, sequence);
            send_message("pong", sequence);
        }
    }

    void run_background_peer() {
        std::thread sender(&RTT_Component::send_background_messages, this);
        receive_background_traffic();
        sender.join();
    }

private:
    const RTT_Config _config;
    const int _vm_id;
    int _matched_send_count;
    int _matched_receive_count;
    int _background_send_count;
    int _background_receive_count;
    std::vector<unsigned long long> _samples_us;
};

} // namespace

int main() {
    const RTT_Config config = {
        "rtt-benchmark",
        20,
        20,
        20,
        200000,
        200000,
    };

    Vehicle vehicle;
    vehicle.add_component(new RTT_Component(config), 0x0000);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

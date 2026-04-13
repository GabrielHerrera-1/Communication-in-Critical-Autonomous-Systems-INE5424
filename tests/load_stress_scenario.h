#ifndef LOAD_STRESS_SCENARIO_H
#define LOAD_STRESS_SCENARIO_H

#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>

namespace load_stress_scenario {

static const int VM_COUNT = 5;

struct Config {
    const char * label;
    int rounds;
    int messages_per_round;
    unsigned int startup_delay_sec;
    uint16_t port;
};

inline int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[load-stress] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }

    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[load-stress] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * token = std::strtok(line, " "); token; token = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(token, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[load-stress] vm_id invalido no cmdline: "
                          << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[load-stress] parametro so2.vm_id ausente no cmdline" << std::endl;
    std::exit(1);
}

inline void build_payload(
    char * buffer,
    std::size_t size,
    const Config & config,
    int round,
    int sender_id,
    int sequence
) {
    std::snprintf(buffer, size, "%s:r%d:vm%d:m%d",
                  config.label, round, sender_id, sequence);
}

class Component_Impl : public Component {
public:
    explicit Component_Impl(const Config & config)
        : Component(config.label),
          _config(config),
          _vm_id(detect_vm_id()),
          _send_count(0),
          _receive_count(0) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[" << _config.label << "][vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        std::cout << "[" << _config.label << "][vm" << _vm_id
                  << "] iniciando cenario de carga." << std::endl;

        if (_config.startup_delay_sec > 0) {
            sleep(_config.startup_delay_sec);
        }

        for (int round = 1; round <= _config.rounds; ++round) {
            send_round(round);
            std::cout << "[" << _config.label << "][vm" << _vm_id
                      << "] rodada " << round << " enviada." << std::endl;
            receive_round(round);
            std::cout << "[" << _config.label << "][vm" << _vm_id
                      << "] rodada " << round << " recebida." << std::endl;
        }

        const int expected_send = _config.rounds * _config.messages_per_round;
        const int expected_receive =
            (VM_COUNT - 1) * _config.rounds * _config.messages_per_round;

        if (_send_count != expected_send || _receive_count != expected_receive) {
            std::cerr << "[" << _config.label << "][vm" << _vm_id
                      << "] contagem final inconsistente: send=" << _send_count
                      << " receive=" << _receive_count << std::endl;
            std::exit(1);
        }

        std::cout << "[" << _config.label << "][vm" << _vm_id
                  << "] validado com " << _send_count
                  << " envios e " << _receive_count
                  << " recebimentos." << std::endl;
    }

    Port logical_port() const override {
        return _config.port;
    }

private:
    void send_round(int round) {
        for (int sequence = 1; sequence <= _config.messages_per_round; ++sequence) {
            char payload[96];
            build_payload(payload, sizeof(payload), _config, round, _vm_id, sequence);

            Message message(payload, std::strlen(payload) + 1);
            if (!_communicator->send(&message)) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] falha ao enviar " << payload << std::endl;
                std::exit(1);
            }

            ++_send_count;
        }
    }

    void receive_round(int expected_round) {
        const int expected_total = (VM_COUNT - 1) * _config.messages_per_round;
        std::vector<unsigned char> seen(
            (VM_COUNT + 1) * (_config.messages_per_round + 1), 0
        );

        int round_receive_count = 0;
        while (round_receive_count < expected_total) {
            Message message;
            if (!_communicator->receive(&message)) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] falha ao receber mensagem" << std::endl;
                std::exit(1);
            }

            const char * payload =
                reinterpret_cast<const char *>(message.data());

            char parsed_label[64];
            int round = 0;
            int sender_id = 0;
            int sequence = 0;

            if (std::sscanf(payload, "%63[^:]:r%d:vm%d:m%d",
                            parsed_label, &round, &sender_id, &sequence) != 4) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] payload invalido: " << payload << std::endl;
                std::exit(1);
            }

            if (std::strcmp(parsed_label, _config.label) != 0 ||
                round != expected_round ||
                sender_id < 1 || sender_id > VM_COUNT ||
                sender_id == _vm_id ||
                sequence < 1 || sequence > _config.messages_per_round) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] metadados invalidos: " << payload << std::endl;
                std::exit(1);
            }

            if (message.origin().port != _config.port) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] porta de origem inesperada: "
                          << message.origin().port << std::endl;
                std::exit(1);
            }

            const int idx = sender_id * (_config.messages_per_round + 1) + sequence;
            if (seen[idx]) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] mensagem duplicada: " << payload << std::endl;
                std::exit(1);
            }

            seen[idx] = 1;
            ++round_receive_count;
            ++_receive_count;
        }

        for (int sender_id = 1; sender_id <= VM_COUNT; ++sender_id) {
            if (sender_id == _vm_id) {
                continue;
            }
            for (int sequence = 1; sequence <= _config.messages_per_round; ++sequence) {
                const int idx = sender_id * (_config.messages_per_round + 1) + sequence;
                if (!seen[idx]) {
                    std::cerr << "[" << _config.label << "][vm" << _vm_id
                              << "] faltou mensagem de vm" << sender_id
                              << " na rodada " << expected_round
                              << " seq " << sequence << std::endl;
                    std::exit(1);
                }
            }
        }
    }

private:
    const Config _config;
    const int _vm_id;
    int _send_count;
    int _receive_count;
};

inline int run(const Config & config) {
    Vehicle vehicle;
    vehicle.add_component(new Component_Impl(config), config.port);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

} // namespace load_stress_scenario

#endif

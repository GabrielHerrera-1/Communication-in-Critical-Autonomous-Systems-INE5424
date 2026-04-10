#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

struct Concurrent_Scenario_Config {
    const char * label;
    int rounds;
    int messages_per_round;
    unsigned int startup_delay_sec;
    unsigned int inter_message_delay_usec;
};

static const int VEHICLE_COUNT = 5;

int detect_vm_id() {
    // cada VM recebe so2.vm_id=N via cmdline do kernel no runner.
    // lemos /proc/cmdline para descobrir qual identidade logica esta rodando aqui.
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[mesh-concurrent] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }

    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[mesh-concurrent] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * token = std::strtok(line, " "); token; token = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(token, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VEHICLE_COUNT) {
                std::cerr << "[mesh-concurrent] vm_id invalido no cmdline: "
                          << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[mesh-concurrent] parametro so2.vm_id ausente no cmdline" << std::endl;
    std::exit(1);
}

void build_payload(
    char * buffer,
    std::size_t size,
    const Concurrent_Scenario_Config & config,
    int round,
    int sender_id,
    int sequence
) {
    // payload simples de teste. round + vm + sequencia permitem validar
    // integridade, completude e duplicatas do cenario.
    std::snprintf(
        buffer,
        size,
        "%s:r%d:vm%d:m%d",
        config.label,
        round,
        sender_id,
        sequence
    );
}

class Mesh_Component : public Component {
public:
    explicit Mesh_Component(const Concurrent_Scenario_Config & config)
        : Component("mesh-gateway"),
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
                  << "] iniciando teste concorrente." << std::endl;

        if (_config.startup_delay_sec > 0) {
            // espera todas as VMs subirem antes de iniciar o trafego
            sleep(_config.startup_delay_sec);
        }

        // envio e recepcao acontecem ao mesmo tempo: uma thread envia
        // enquanto a thread principal fica bloqueando em receive()
        std::thread sender(&Mesh_Component::send_all_messages, this);
        receive_all_messages();
        sender.join();

        std::cout << "[" << _config.label << "][vm" << _vm_id
                  << "] concluido com " << _send_count
                  << " envios e " << _receive_count
                  << " recebimentos validados." << std::endl;
    }

    Port logical_port() const override {
        return 0x0000;
    }

private:
    void send_all_messages() {
        for (int round = 1; round <= _config.rounds; ++round) {
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
                std::cout << "[" << _config.label << "][vm" << _vm_id
                          << "] enviou: " << payload << std::endl;

                // pequeno espacamento para o trafego nao sair todo de uma vez
                if (_config.inter_message_delay_usec > 0 &&
                    !(round == _config.rounds &&
                      sequence == _config.messages_per_round)) {
                    usleep(_config.inter_message_delay_usec);
                }
            }
        }
    }

    void receive_all_messages() {
        // cada VM recebe tudo das outras 4:
        // (5 - 1) * 2 rounds * 3 msgs = 24 mensagens
        const int expected_total =
            (VEHICLE_COUNT - 1) * _config.rounds * _config.messages_per_round;

        // tabela achatada para marcar quais combinacoes (round, sender, sequence)
        // ja chegaram. isso nos permite detectar duplicatas e faltas.
        std::vector<unsigned char> seen(
            (_config.rounds + 1) * (VEHICLE_COUNT + 1) * (_config.messages_per_round + 1),
            0
        );

        while (_receive_count < expected_total) {
            Message message;
            if (!_communicator->receive(&message)) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] falha ao receber mensagem concorrente" << std::endl;
                std::exit(1);
            }

            const char * payload =
                reinterpret_cast<const char *>(message.data());

            char parsed_label[64];
            int round = 0;
            int sender_id = 0;
            int sequence = 0;

            // formato esperado:
            // mesh-concurrent:r<round>:vm<sender_id>:m<sequence>
            if (std::sscanf(
                    payload,
                    "%63[^:]:r%d:vm%d:m%d",
                    parsed_label,
                    &round,
                    &sender_id,
                    &sequence
                ) != 4) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] payload invalido: " << payload << std::endl;
                std::exit(1);
            }

            // garante que a mensagem pertence a este cenario de teste
            if (std::strcmp(parsed_label, _config.label) != 0) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] label inesperado: " << payload << std::endl;
                std::exit(1);
            }

            // valida os metadados extraidos do payload.
            // sender_id == _vm_id indica mensagem propria, o que nao deve acontecer aqui.
            if (round < 1 || round > _config.rounds ||
                sender_id < 1 || sender_id > VEHICLE_COUNT ||
                sender_id == _vm_id ||
                sequence < 1 || sequence > _config.messages_per_round) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] metadados invalidos no payload: "
                          << payload << std::endl;
                std::exit(1);
            }

            // transforma (round, sender, sequence) em um indice unico no vetor seen
            const int idx =
                ((round * (VEHICLE_COUNT + 1)) + sender_id) *
                (_config.messages_per_round + 1) + sequence;

            // se ja marcamos esse indice antes, a mensagem chegou duplicada
            if (seen[idx]) {
                std::cerr << "[" << _config.label << "][vm" << _vm_id
                          << "] mensagem duplicada: " << payload << std::endl;
                std::exit(1);
            }

            seen[idx] = 1;
            ++_receive_count;

            std::cout << "[" << _config.label << "][vm" << _vm_id
                      << "] recebeu: " << payload << std::endl;
        }

        // checagem final de completude: percorre tudo que deveria ter chegado
        // e falha se alguma combinacao esperada nao apareceu.
        for (int round = 1; round <= _config.rounds; ++round) {
            for (int sender_id = 1; sender_id <= VEHICLE_COUNT; ++sender_id) {
                if (sender_id == _vm_id) {
                    continue;
                }
                for (int sequence = 1; sequence <= _config.messages_per_round; ++sequence) {
                    const int idx =
                        ((round * (VEHICLE_COUNT + 1)) + sender_id) *
                        (_config.messages_per_round + 1) + sequence;
                    if (!seen[idx]) {
                        std::cerr << "[" << _config.label << "][vm" << _vm_id
                                  << "] faltou mensagem de vm" << sender_id
                                  << " round " << round
                                  << " seq " << sequence << std::endl;
                        std::exit(1);
                    }
                }
            }
        }
    }

private:
    const Concurrent_Scenario_Config _config;
    const int _vm_id;
    int _send_count;
    int _receive_count;
};

} // namespace

int main() {
    const Concurrent_Scenario_Config config = {
        "mesh-concurrent",
        2,
        3,
        20,
        200000,
    };

    Vehicle vehicle;
    vehicle.add_component(new Mesh_Component(config), 0x0000);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

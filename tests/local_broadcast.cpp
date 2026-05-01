#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

static const unsigned int MESSAGE_COUNT_PER_SENDER = 2;
static const unsigned int SENDER_COUNT = 3;

struct Participant_Config {
    const char * id;
    uint16_t port;
    bool sends;
    char sender_tag;
};

static const Participant_Config PARTICIPANTS[] = {
    {"local-broadcast-sender-a", Component_Ports::TEST_LOCAL_BROADCAST_A, true,  'A'},
    {"local-broadcast-sender-b", Component_Ports::TEST_LOCAL_BROADCAST_B, true,  'B'},
    {"local-broadcast-sender-c", Component_Ports::TEST_LOCAL_BROADCAST_C, true,  'C'},
    {"local-broadcast-listener-d", Component_Ports::TEST_LOCAL_BROADCAST_D, false, 'D'},
    {"local-broadcast-listener-e", Component_Ports::TEST_LOCAL_BROADCAST_E, false, 'E'},
};

bool is_sender_port(uint16_t port) {
    for (const auto & participant : PARTICIPANTS) {
        if (participant.port == port) {
            return participant.sends;
        }
    }
    return false;
}

char sender_tag_for_port(uint16_t port) {
    for (const auto & participant : PARTICIPANTS) {
        if (participant.port == port) {
            return participant.sender_tag;
        }
    }
    return '?';
}

unsigned int expected_receive_count(bool self_sends) {
    const unsigned int total_broadcast_messages = SENDER_COUNT * MESSAGE_COUNT_PER_SENDER;
    if (!self_sends) {
        return total_broadcast_messages;
    }

    return total_broadcast_messages - MESSAGE_COUNT_PER_SENDER;
}

void build_payload(char * buffer, std::size_t size, char sender_tag, unsigned int sequence) {
    std::snprintf(buffer, size, "local-broadcast:sender=%c:seq=%u", sender_tag, sequence);
}

bool parse_payload(const char * payload, char * sender_tag, unsigned int * sequence) {
    char label[64];
    char parsed_sender = '\0';
    unsigned int parsed_sequence = 0;

    if (std::sscanf(payload, "%63[^:]:sender=%c:seq=%u", label, &parsed_sender, &parsed_sequence) != 3) {
        return false;
    }

    if (std::strcmp(label, "local-broadcast") != 0) {
        return false;
    }

    if (sender_tag) {
        *sender_tag = parsed_sender;
    }

    if (sequence) {
        *sequence = parsed_sequence;
    }

    return true;
}

class Local_Broadcast_Participant : public Component {
public:
    explicit Local_Broadcast_Participant(const Participant_Config & config)
        : Component(config.id),
          _config(config) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[local-broadcast] " << id()
                      << " sem communicator" << std::endl;
            std::exit(1);
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));

        if (_config.sends) {
            send_messages();
        }

        receive_expected_messages();
    }

    Port logical_port() const override {
        return _config.port;
    }

private:
    void send_messages() {
        for (unsigned int sequence = 1; sequence <= MESSAGE_COUNT_PER_SENDER; ++sequence) {
            char payload[96];
            build_payload(payload, sizeof(payload), _config.sender_tag, sequence);

            Message outbound(payload, std::strlen(payload) + 1);
            if (!_communicator->send(&outbound)) {
                std::cerr << "[local-broadcast] " << id()
                          << " falha ao enviar " << payload << std::endl;
                std::exit(1);
            }

            std::cout << "[local-broadcast] " << id()
                      << " enviou: " << payload << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void receive_expected_messages() {
        const unsigned int expected = expected_receive_count(_config.sends);
        bool seen[SENDER_COUNT][MESSAGE_COUNT_PER_SENDER] = {};
        unsigned int received = 0;

        while (received < expected) {
            Message inbound;
            if (!_communicator->receive(&inbound)) {
                std::cerr << "[local-broadcast] " << id()
                          << " falhou ao receber" << std::endl;
                std::exit(1);
            }

            validate_message(inbound, seen);
            ++received;
        }

        unsigned int expected_unique = 0;
        for (const auto & participant : PARTICIPANTS) {
            if (!participant.sends) {
                continue;
            }

            if (_config.sends && participant.sender_tag == _config.sender_tag) {
                continue;
            }

            for (unsigned int sequence = 0; sequence < MESSAGE_COUNT_PER_SENDER; ++sequence) {
                if (!seen[participant.sender_tag - 'A'][sequence]) {
                    std::cerr << "[local-broadcast] " << id()
                              << " nao recebeu sender=" << participant.sender_tag
                              << " seq=" << (sequence + 1) << std::endl;
                    std::exit(1);
                }
                ++expected_unique;
            }
        }

        if (expected_unique != expected) {
            std::cerr << "[local-broadcast] " << id()
                      << " contagem final inconsistente" << std::endl;
            std::exit(1);
        }
    }

    void validate_message(const Message & inbound, bool seen[SENDER_COUNT][MESSAGE_COUNT_PER_SENDER]) {
        const char * payload = reinterpret_cast<const char *>(inbound.data());

        char sender_tag = '\0';
        unsigned int sequence = 0;
        if (!parse_payload(payload, &sender_tag, &sequence)) {
            std::cerr << "[local-broadcast] " << id()
                      << " payload invalido: " << payload << std::endl;
            std::exit(1);
        }

        if (sender_tag < 'A' || sender_tag >= static_cast<char>('A' + SENDER_COUNT)) {
            std::cerr << "[local-broadcast] " << id()
                      << " sender invalido: " << sender_tag << std::endl;
            std::exit(1);
        }

        if (sequence < 1 || sequence > MESSAGE_COUNT_PER_SENDER) {
            std::cerr << "[local-broadcast] " << id()
                      << " sequencia invalida: " << sequence << std::endl;
            std::exit(1);
        }

        const uint16_t origin_port = inbound.origin().port;
        if (!is_sender_port(origin_port)) {
            std::cerr << "[local-broadcast] " << id()
                      << " porta de origem invalida: " << origin_port << std::endl;
            std::exit(1);
        }

        if (sender_tag_for_port(origin_port) != sender_tag) {
            std::cerr << "[local-broadcast] " << id()
                      << " sender no payload nao bate com origin.port" << std::endl;
            std::exit(1);
        }

        if (_config.sends && origin_port == _config.port) {
            std::cerr << "[local-broadcast] " << id()
                      << " recebeu a propria mensagem (self-drop falhou)" << std::endl;
            std::exit(1);
        }

        const unsigned int sender_index = static_cast<unsigned int>(sender_tag - 'A');
        const unsigned int sequence_index = sequence - 1;
        if (seen[sender_index][sequence_index]) {
            std::cerr << "[local-broadcast] " << id()
                      << " mensagem duplicada de sender=" << sender_tag
                      << " seq=" << sequence << std::endl;
            std::exit(1);
        }

        seen[sender_index][sequence_index] = true;

        std::cout << "[local-broadcast] " << id()
                  << " recebeu: " << payload << std::endl;
    }

private:
    const Participant_Config _config;
};

} // namespace

int main() {
    Vehicle vehicle;
    for (const auto & participant : PARTICIPANTS) {
        vehicle.add_component(new Local_Broadcast_Participant(participant), participant.port);
    }

    vehicle.initialize();
    vehicle.run();

    std::cout << "[local-broadcast] cenario validado." << std::endl;
    return 0;
}

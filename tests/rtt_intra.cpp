#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <unistd.h>
#include <vector>

namespace {

static const char RTT_LABEL[] = "rtt-intra";
static const int MAX_SAMPLES = 50000;
static const unsigned int STARTUP_DELAY_SEC = 5;
static const unsigned int INTER_SAMPLE_DELAY_USEC = 0;
static const unsigned int RUN_DURATION_SEC = 3600;
static const Ethernet::Address INTERNAL_ADDRESS = Ethernet::Address::INTERNAL;

void build_payload(char * buffer,
                   std::size_t size,
                   const char * kind,
                   int sequence) {
    std::snprintf(buffer, size, "%s:%s:%d", RTT_LABEL, kind, sequence);
}

bool parse_payload(const char * payload,
                   char * kind,
                   std::size_t kind_size,
                   int * sequence) {
    char label[32];
    char parsed_kind[16];
    int parsed_sequence = 0;

    if (std::sscanf(payload, "%31[^:]:%15[^:]:%d", label, parsed_kind, &parsed_sequence) != 3) {
        return false;
    }

    if (std::strcmp(label, RTT_LABEL) != 0) {
        return false;
    }

    if (kind && kind_size > 0) {
        std::snprintf(kind, kind_size, "%s", parsed_kind);
    }

    if (sequence) {
        *sequence = parsed_sequence;
    }

    return true;
}

unsigned long long monotonic_raw_us() {
    timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        std::perror("[rtt-intra] clock_gettime");
        std::exit(1);
    }

    return static_cast<unsigned long long>(ts.tv_sec) * 1000000ULL +
           static_cast<unsigned long long>(ts.tv_nsec / 1000ULL);
}

class RTT_Intra_Initiator : public Component {
public:
    RTT_Intra_Initiator()
        : Component("rtt-intra-initiator") {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_RTT_INTRA_INITIATOR;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[rtt-intra][initiator] communicator ausente" << std::endl;
            std::exit(1);
        }

        sleep(STARTUP_DELAY_SEC);

        _samples_us.reserve(MAX_SAMPLES);

        const unsigned long long deadline_us =
            monotonic_raw_us() +
            static_cast<unsigned long long>(RUN_DURATION_SEC) * 1000000ULL;

        for (int sequence = 1; sequence <= MAX_SAMPLES; ++sequence) {
            if (monotonic_raw_us() >= deadline_us) {
                break;
            }

            const unsigned long long start_us = monotonic_raw_us();
            send_message("ping", sequence);
            receive_expected("pong", sequence, Component_Ports::TEST_RTT_INTRA_RESPONDER);
            const unsigned long long end_us = monotonic_raw_us();

            _samples_us.push_back(end_us - start_us);

            if (INTER_SAMPLE_DELAY_USEC > 0) {
                usleep(INTER_SAMPLE_DELAY_USEC);
            }
        }

        send_message("stop", 0);

        dump_results();
        std::cout << "[rtt-intra][initiator] RTT intra benchmark concluido." << std::endl;
    }

private:
    void send_message(const char * kind, int sequence) {
        char payload[64];
        build_payload(payload, sizeof(payload), kind, sequence);

        Message message(payload, std::strlen(payload) + 1);
        if (!_communicator->send(&message)) {
            std::cerr << "[rtt-intra][initiator] falha ao enviar " << payload << std::endl;
            std::exit(1);
        }
    }

    void receive_expected(const char * expected_kind,
                          int expected_sequence,
                          Port expected_origin_port) {
        Message message;
        if (!_communicator->receive(&message)) {
            std::cerr << "[rtt-intra][initiator] falha ao receber pong" << std::endl;
            std::exit(1);
        }

        const char * payload = reinterpret_cast<const char *>(message.data());
        char kind[16];
        int sequence = 0;
        if (!parse_payload(payload, kind, sizeof(kind), &sequence)) {
            std::cerr << "[rtt-intra][initiator] payload invalido: " << payload << std::endl;
            std::exit(1);
        }

        if (std::strcmp(kind, expected_kind) != 0 || sequence != expected_sequence) {
            std::cerr << "[rtt-intra][initiator] mensagem inesperada: " << payload << std::endl;
            std::exit(1);
        }

        if (message.origin().port != expected_origin_port) {
            std::cerr << "[rtt-intra][initiator] porta de origem inesperada: "
                      << message.origin().port << std::endl;
            std::exit(1);
        }

        if (message.origin().address != INTERNAL_ADDRESS) {
            std::cerr << "[rtt-intra][initiator] origem nao eh interna" << std::endl;
            std::exit(1);
        }
    }

    void dump_results() const {
        if (_samples_us.empty()) {
            std::cerr << "[rtt-intra][initiator] nenhuma amostra RTT coletada" << std::endl;
            std::exit(1);
        }

        for (std::size_t i = 0; i < _samples_us.size(); ++i) {
            std::cout << "[rtt-intra][initiator] RTT_SAMPLE seq=" << (i + 1)
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

        std::cout << "[rtt-intra][initiator] RTT_RESULT samples=" << _samples_us.size()
                  << " avg_us=" << avg_us
                  << " min_us=" << min_us
                  << " max_us=" << max_us << std::endl;
    }

    std::vector<unsigned long long> _samples_us;
};

class RTT_Intra_Responder : public Component {
public:
    RTT_Intra_Responder()
        : Component("rtt-intra-responder") {}

    void initialize() override {}

    Port logical_port() const override {
        return Component_Ports::TEST_RTT_INTRA_RESPONDER;
    }

    void run() override {
        if (!_communicator) {
            std::cerr << "[rtt-intra][responder] communicator ausente" << std::endl;
            std::exit(1);
        }

        sleep(STARTUP_DELAY_SEC);

        for (int sequence = 1; sequence <= MAX_SAMPLES; ++sequence) {
            Message message;
            if (!_communicator->receive(&message)) {
                std::cerr << "[rtt-intra][responder] falha ao receber" << std::endl;
                std::exit(1);
            }

            const char * payload = reinterpret_cast<const char *>(message.data());
            char kind[16];
            int seq = 0;
            if (!parse_payload(payload, kind, sizeof(kind), &seq)) {
                std::cerr << "[rtt-intra][responder] payload invalido: " << payload << std::endl;
                std::exit(1);
            }

            if (std::strcmp(kind, "stop") == 0) {
                break;
            }

            if (std::strcmp(kind, "ping") != 0 || seq != sequence) {
                std::cerr << "[rtt-intra][responder] mensagem inesperada: " << payload << std::endl;
                std::exit(1);
            }

            if (message.origin().port != Component_Ports::TEST_RTT_INTRA_INITIATOR) {
                std::cerr << "[rtt-intra][responder] porta de origem inesperada: "
                          << message.origin().port << std::endl;
                std::exit(1);
            }

            if (message.origin().address != INTERNAL_ADDRESS) {
                std::cerr << "[rtt-intra][responder] origem nao eh interna" << std::endl;
                std::exit(1);
            }

            send_message("pong", sequence);
        }

        std::cout << "[rtt-intra][responder] RTT intra benchmark concluido." << std::endl;
    }

private:
    void send_message(const char * kind, int sequence) {
        char payload[64];
        build_payload(payload, sizeof(payload), kind, sequence);

        Message message(payload, std::strlen(payload) + 1);
        if (!_communicator->send(&message)) {
            std::cerr << "[rtt-intra][responder] falha ao enviar " << payload << std::endl;
            std::exit(1);
        }
    }
};

} // namespace

int main() {
    Vehicle vehicle;
    vehicle.add_component(new RTT_Intra_Initiator(),
                          Component_Ports::TEST_RTT_INTRA_INITIATOR);
    vehicle.add_component(new RTT_Intra_Responder(),
                          Component_Ports::TEST_RTT_INTRA_RESPONDER);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

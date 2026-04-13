#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/application/component_ports.h"
#include "../src/communication/message.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <unistd.h>
#include <vector>

namespace {

static const int VM_COUNT = 5;
static const int SENDER_VM_ID = 1;
static const int MESSAGES_FROM_SENDER = 2000;
static const useconds_t INTER_SEND_US = 500;
static const useconds_t POLL_INTERVAL_US = 1000;
static const uint64_t QUIET_PERIOD_US = 2ull * 1000ull * 1000ull;
static const uint64_t MAX_WAIT_US = 15ull * 1000ull * 1000ull;
static const unsigned int STARTUP_DELAY_SEC = 15;

uint64_t now_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ull +
           static_cast<uint64_t>(ts.tv_nsec / 1000ull);
}

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[network-loss-stress] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }

    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[network-loss-stress] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * token = std::strtok(line, " "); token; token = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(token, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[network-loss-stress] vm_id invalido: " << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[network-loss-stress] parametro so2.vm_id ausente no cmdline" << std::endl;
    std::exit(1);
}

void build_payload(char * buffer, std::size_t size, int sequence) {
    std::snprintf(buffer, size, "network-loss-stress:vm%d:m%d", SENDER_VM_ID, sequence);
}

class Network_Loss_Component : public Component {
public:
    Network_Loss_Component()
        : Component("network-loss-stress"),
          _vm_id(detect_vm_id()),
          _receive_count(0),
          _duplicate_count(0),
          _seen(MESSAGES_FROM_SENDER + 1, 0) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[network-loss-stress][vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        std::cout << "[network-loss-stress][vm" << _vm_id
                  << "] iniciando medicao com intersticio de "
                  << INTER_SEND_US << "us." << std::endl;

        sleep(STARTUP_DELAY_SEC);

        if (_vm_id == SENDER_VM_ID) {
            run_sender();
            return;
        }

        run_receiver();
    }

    Port logical_port() const override {
        return Component_Ports::TEST_NETWORK_LOSS_STRESS;
    }

private:
    void run_sender() {
        for (int sequence = 1; sequence <= MESSAGES_FROM_SENDER; ++sequence) {
            char payload[96];
            build_payload(payload, sizeof(payload), sequence);

            Message message(payload, std::strlen(payload) + 1);
            if (!_communicator->send(&message)) {
                std::cerr << "[network-loss-stress][vm" << _vm_id
                          << "] falha ao enviar seq " << sequence << std::endl;
                std::exit(1);
            }

            // No barramento local atual, o proprio writer ainda precisa
            // avancar pelos slots que publicou para liberar os leitores
            // pendentes. Como este cenario quer medir a rede e nao ficar
            // preso no self-drop da SHM, damos um passo de dispatch entre os
            // envios. Se nao houver mensagem "real" para a aplicacao, o
            // try_receive() so devolve false, mas mesmo assim ajuda a drenar
            // os slots locais que pertencem ao emissor.
            Message scratch;
            (void)_communicator->try_receive(&scratch);

            usleep(INTER_SEND_US);
        }

        std::cout << "[network-loss-stress][vm" << _vm_id
                  << "] medicao concluida. papel=sender enviados="
                  << MESSAGES_FROM_SENDER
                  << " intersticio_us=" << INTER_SEND_US
                  << std::endl;
    }

    void run_receiver() {
        const uint64_t wait_start = now_us();
        uint64_t last_progress = wait_start;
        bool saw_any = false;

        while ((now_us() - wait_start) < MAX_WAIT_US) {
            const int drained = drain_ready_messages();
            if (drained > 0) {
                saw_any = true;
                last_progress = now_us();
                continue;
            }

            if (saw_any && (now_us() - last_progress) >= QUIET_PERIOD_US) {
                break;
            }

            usleep(POLL_INTERVAL_US);
        }

        const int missing = count_missing();
        std::cout << "[network-loss-stress][vm" << _vm_id
                  << "] medicao concluida. papel=receiver esperados="
                  << MESSAGES_FROM_SENDER
                  << " recebidos_unicos=" << _receive_count
                  << " perdidos=" << missing
                  << " duplicados=" << _duplicate_count
                  << " intersticio_us=" << INTER_SEND_US
                  << std::endl;
    }

    int drain_ready_messages() {
        int drained = 0;
        while (true) {
            Message message;
            if (!_communicator->try_receive(&message)) {
                break;
            }

            process_message(message);
            ++drained;
        }

        return drained;
    }

    void process_message(const Message & message) {
        const char * payload = reinterpret_cast<const char *>(message.data());

        int sender_id = 0;
        int sequence = 0;
        if (std::sscanf(payload, "network-loss-stress:vm%d:m%d",
                        &sender_id, &sequence) != 2) {
            std::cerr << "[network-loss-stress][vm" << _vm_id
                      << "] payload invalido: " << payload << std::endl;
            std::exit(1);
        }

        if (sender_id != SENDER_VM_ID ||
            sequence < 1 || sequence > MESSAGES_FROM_SENDER) {
            std::cerr << "[network-loss-stress][vm" << _vm_id
                      << "] metadados invalidos: " << payload << std::endl;
            std::exit(1);
        }

        if (message.origin().port != Component_Ports::TEST_NETWORK_LOSS_STRESS) {
            std::cerr << "[network-loss-stress][vm" << _vm_id
                      << "] porta de origem inesperada: "
                      << message.origin().port << std::endl;
            std::exit(1);
        }

        if (_seen[sequence]) {
            ++_duplicate_count;
            return;
        }

        _seen[sequence] = 1;
        ++_receive_count;
    }

    int count_missing() const {
        int missing = 0;
        for (int sequence = 1; sequence <= MESSAGES_FROM_SENDER; ++sequence) {
            if (!_seen[sequence]) {
                ++missing;
            }
        }
        return missing;
    }

private:
    const int _vm_id;
    int _receive_count;
    int _duplicate_count;
    std::vector<unsigned char> _seen;
};

} // namespace

int main() {
    Vehicle vehicle;
    vehicle.add_component(new Network_Loss_Component());
    vehicle.initialize();
    vehicle.run();
    return 0;
}

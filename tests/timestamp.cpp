#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>

namespace {

static const int MESSAGE_COUNT = 100;

// sender apenas envia — nunca lê broadcast, evita acumular buffer
class Timestamp_Sender : public Component {
public:
    Timestamp_Sender() : Component("timestamp-sender") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) { std::exit(1); }

        std::this_thread::sleep_for(std::chrono::seconds(2));

        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            const char payload[] = "ts";
            Message m(payload, sizeof(payload));
            if (!_communicator->send(&m)) {
                std::cerr << "[timestamp] sender: falha ao enviar msg " << i << std::endl;
                std::exit(1);
            }
        }
    }

    bool subscribe_logical_broadcast() const override { return false; }
    Port logical_port() const override { return Component_Ports::TEST_TIMESTAMP_SENDER; }
};

class Timestamp_Receiver : public Component {
public:
    Timestamp_Receiver() : Component("timestamp-receiver") {}
    void initialize() override {}

    void run() override {
        if (!_communicator) { std::exit(1); }

        int64_t last_ts = 0;
        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            Message m;
            if (!_communicator->receive(&m)) {
                std::cerr << "[timestamp] receiver: falha ao receber msg " << i << std::endl;
                std::exit(1);
            }

            int64_t ts = m.timestamp();

            if (ts == 0) {
                std::cerr << "[timestamp] timestamp zero na msg " << i << std::endl;
                std::exit(1);
            }

            if (ts <= last_ts) {
                std::cerr << "[timestamp] timestamp nao crescente: "
                          << ts << " <= " << last_ts
                          << " (msg " << i << ")" << std::endl;
                std::exit(1);
            }

            last_ts = ts;
        }

        std::cout << "[timestamp] cenario validado: " << MESSAGE_COUNT
                  << " mensagens com timestamps estritamente crescentes." << std::endl;
    }

    Port logical_port() const override { return Component_Ports::TEST_TIMESTAMP_RECEIVER; }
};

} // namespace

int main() {
    Vehicle vehicle;
    vehicle.add_component(new Timestamp_Sender(),   Component_Ports::TEST_TIMESTAMP_SENDER);
    vehicle.add_component(new Timestamp_Receiver(), Component_Ports::TEST_TIMESTAMP_RECEIVER);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

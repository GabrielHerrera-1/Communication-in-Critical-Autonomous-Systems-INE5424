#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

static const char PAYLOAD[] = "local-broadcast:payload";
static const Ethernet::Address INTERNAL_ADDRESS(0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
static const uint16_t SENDER_PORT = 0x3101;
static const uint16_t RECEIVER_A_PORT = 0x3102;
static const uint16_t RECEIVER_B_PORT = 0x3103;

class Local_Broadcast_Sender : public Component {
public:
    Local_Broadcast_Sender() : Component("local-broadcast-sender") {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[local-broadcast] sender sem communicator" << std::endl;
            std::exit(1);
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));

        Message outbound(PAYLOAD, sizeof(PAYLOAD));
        if (!_communicator->send(&outbound)) {
            std::cerr << "[local-broadcast] falha ao enviar payload local" << std::endl;
            std::exit(1);
        }
    }

    Port logical_port() const override {
        return SENDER_PORT;
    }
};

class Local_Broadcast_Receiver : public Component {
public:
    explicit Local_Broadcast_Receiver(const char * id, Port port)
        : Component(id), _logical_port(port) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[local-broadcast] " << id()
                      << " sem communicator" << std::endl;
            std::exit(1);
        }

        Message inbound;
        if (!_communicator->receive(&inbound)) {
            std::cerr << "[local-broadcast] " << id()
                      << " falhou ao receber" << std::endl;
            std::exit(1);
        }

        const char * payload = reinterpret_cast<const char *>(inbound.data());
        if (std::strcmp(payload, PAYLOAD) != 0) {
            std::cerr << "[local-broadcast] " << id()
                      << " payload inesperado: " << payload << std::endl;
            std::exit(1);
        }

        if (inbound.origin().port != SENDER_PORT) {
            std::cerr << "[local-broadcast] " << id()
                      << " porta de origem inesperada: "
                      << inbound.origin().port << std::endl;
            std::exit(1);
        }

        if (inbound.origin().address != INTERNAL_ADDRESS) {
            std::cerr << "[local-broadcast] " << id()
                      << " endereco de origem local inesperado" << std::endl;
            std::exit(1);
        }
    }

    Port logical_port() const override {
        return _logical_port;
    }

private:
    Port _logical_port;
};

} // namespace

int main() {
    Vehicle vehicle;
    vehicle.add_component(new Local_Broadcast_Sender());
    vehicle.add_component(new Local_Broadcast_Receiver("local-broadcast-receiver-a", RECEIVER_A_PORT));
    vehicle.add_component(new Local_Broadcast_Receiver("local-broadcast-receiver-b", RECEIVER_B_PORT));
    vehicle.initialize();
    vehicle.run();
    std::cout << "[local-broadcast] cenario validado." << std::endl;
    return 0;
}

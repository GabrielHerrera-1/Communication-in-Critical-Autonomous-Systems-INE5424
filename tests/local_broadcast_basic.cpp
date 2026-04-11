#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

static const char PAYLOAD[] = "local-broadcast-basic:payload";
static const Ethernet::Address INTERNAL_ADDRESS = Ethernet::Address::INTERNAL;
static const uint16_t SENDER_PORT = Component_Ports::TEST_LOCAL_BROADCAST_BASIC_SENDER;
static const uint16_t RECEIVER_A_PORT = Component_Ports::TEST_LOCAL_BROADCAST_BASIC_RECEIVER_A;
static const uint16_t RECEIVER_B_PORT = Component_Ports::TEST_LOCAL_BROADCAST_BASIC_RECEIVER_B;

class Local_Broadcast_Sender : public Component {
public:
    Local_Broadcast_Sender() : Component("local-broadcast-basic-sender") {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[local-broadcast-basic] sender sem communicator" << std::endl;
            std::exit(1);
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));

        Message outbound(PAYLOAD, sizeof(PAYLOAD));
        if (!_communicator->send(&outbound)) {
            std::cerr << "[local-broadcast-basic] falha ao enviar payload local" << std::endl;
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
            std::cerr << "[local-broadcast-basic] " << id()
                      << " sem communicator" << std::endl;
            std::exit(1);
        }

        Message inbound;
        if (!_communicator->receive(&inbound)) {
            std::cerr << "[local-broadcast-basic] " << id()
                      << " falhou ao receber" << std::endl;
            std::exit(1);
        }

        const char * payload = reinterpret_cast<const char *>(inbound.data());
        if (std::strcmp(payload, PAYLOAD) != 0) {
            std::cerr << "[local-broadcast-basic] " << id()
                      << " payload inesperado: " << payload << std::endl;
            std::exit(1);
        }

        if (inbound.origin().port != SENDER_PORT) {
            std::cerr << "[local-broadcast-basic] " << id()
                      << " porta de origem inesperada: "
                      << inbound.origin().port << std::endl;
            std::exit(1);
        }

        if (inbound.origin().address != INTERNAL_ADDRESS) {
            std::cerr << "[local-broadcast-basic] " << id()
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
    vehicle.add_component(new Local_Broadcast_Receiver("local-broadcast-basic-receiver-a", RECEIVER_A_PORT));
    vehicle.add_component(new Local_Broadcast_Receiver("local-broadcast-basic-receiver-b", RECEIVER_B_PORT));
    vehicle.initialize();
    vehicle.run();
    std::cout << "[local-broadcast-basic] cenario validado." << std::endl;
    return 0;
}

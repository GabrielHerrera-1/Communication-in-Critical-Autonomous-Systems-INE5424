#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/communication/message.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

namespace {

static const int VM_COUNT = 2;
static const int INITIATOR_VM_ID = 1;
static const int RESPONDER_VM_ID = 2;
static const uint16_t COMPONENT_PORT = Component_Ports::TEST_GATEWAY_PATH;

int detect_vm_id() {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[gateway-path] nao foi possivel abrir /proc/cmdline" << std::endl;
        std::exit(1);
    }

    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[gateway-path] nao foi possivel ler /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * token = std::strtok(line, " "); token; token = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(token, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > VM_COUNT) {
                std::cerr << "[gateway-path] vm_id invalido no cmdline: "
                          << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[gateway-path] parametro so2.vm_id ausente no cmdline" << std::endl;
    std::exit(1);
}

Ethernet::Address expected_vm_mac(int vm_id) {
    return Ethernet::Address(0x52, 0x54, 0x00, 0x12, 0x34, static_cast<uint8_t>(vm_id));
}

class Gateway_Path_Component : public Component {
public:
    explicit Gateway_Path_Component(int vm_id)
        : Component("gateway-path"),
          _vm_id(vm_id) {}

    void initialize() override {}

    void run() override {
        if (!_communicator) {
            std::cerr << "[gateway-path][vm" << _vm_id
                      << "] communicator ausente" << std::endl;
            std::exit(1);
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (_vm_id == INITIATOR_VM_ID) {
            run_initiator();
        } else if (_vm_id == RESPONDER_VM_ID) {
            run_responder();
        } else {
            std::cerr << "[gateway-path] vm_id inesperado: " << _vm_id << std::endl;
            std::exit(1);
        }

        std::cout << "[gateway-path][vm" << _vm_id << "] cenario validado." << std::endl;
    }

    Port logical_port() const override {
        return COMPONENT_PORT;
    }

private:
    void run_initiator() {
        static const char ping[] = "gateway-path:ping";
        static const char pong[] = "gateway-path:pong";

        Message outbound(ping, sizeof(ping));
        if (!_communicator->send(&outbound)) {
            std::cerr << "[gateway-path][vm" << _vm_id
                      << "] falha ao enviar ping" << std::endl;
            std::exit(1);
        }

        Message inbound;
        if (!_communicator->receive(&inbound)) {
            std::cerr << "[gateway-path][vm" << _vm_id
                      << "] falha ao receber pong" << std::endl;
            std::exit(1);
        }

        validate_message(inbound, pong, RESPONDER_VM_ID);
    }

    void run_responder() {
        static const char ping[] = "gateway-path:ping";
        static const char pong[] = "gateway-path:pong";

        Message inbound;
        if (!_communicator->receive(&inbound)) {
            std::cerr << "[gateway-path][vm" << _vm_id
                      << "] falha ao receber ping" << std::endl;
            std::exit(1);
        }

        validate_message(inbound, ping, INITIATOR_VM_ID);

        Message outbound(pong, sizeof(pong));
        if (!_communicator->send(&outbound)) {
            std::cerr << "[gateway-path][vm" << _vm_id
                      << "] falha ao enviar pong" << std::endl;
            std::exit(1);
        }
    }

    void validate_message(const Message & message, const char * expected_payload, int expected_vm_id) {
        const char * payload = reinterpret_cast<const char *>(message.data());
        if (std::strcmp(payload, expected_payload) != 0) {
            std::cerr << "[gateway-path][vm" << _vm_id
                      << "] payload inesperado: " << payload << std::endl;
            std::exit(1);
        }

        if (message.origin().port != COMPONENT_PORT) {
            std::cerr << "[gateway-path][vm" << _vm_id
                      << "] porta de origem inesperada: " << message.origin().port << std::endl;
            std::exit(1);
        }

    }

private:
    int _vm_id;
};

} // namespace

int main() {
    const int vm_id = detect_vm_id();

    Vehicle vehicle;
    vehicle.add_component(new Gateway_Path_Component(vm_id), COMPONENT_PORT);
    vehicle.initialize();
    vehicle.run();
    return 0;
}

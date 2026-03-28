#include "../src/application/vehicle_protocol.h"
#include "../src/communication/communicator.h"
#include "../src/communication/message.h"
#include <iostream>

int main(){
    constexpr Vehicle_Protocol::Port COMPONENT_PORT = 0x0404;

    NIC<RawSocketEngine> nic;
    Vehicle_Protocol protocol(&nic);
    Vehicle_Protocol::Address component(nic.address(), COMPONENT_PORT);
    Communicator<Vehicle_Protocol> communicator(&protocol, component);

    Message message;
    if (!communicator.receive(&message)) {
        std::cerr << "[v2] falha ao receber mensagem" << std::endl;
        return 1;
    }

    std::cout << "[v2] recebido na porta 0x0404: "
              << reinterpret_cast<const char*>(message.data()) << std::endl;
    return 0;
}
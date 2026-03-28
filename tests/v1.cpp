#include "../src/application/vehicle_protocol.h"
#include "../src/communication/communicator.h"
#include "../src/communication/message.h"
#include <iostream>
#include <unistd.h>

int main(){
    constexpr Vehicle_Protocol::Port COMPONENT_PORT = 0x0404;

    NIC<RawSocketEngine> nic;
    Vehicle_Protocol protocol(&nic);
    Vehicle_Protocol::Address component(nic.address(), COMPONENT_PORT);
    Communicator<Vehicle_Protocol> communicator(&protocol, component);

    sleep(2);

    const char payload[] = "vm1: componente 0x0404 enviando broadcast";
    const Message message(payload, sizeof(payload));

    std::cout << "[v1] enviando pela porta 0x0404" << std::endl;
    return communicator.send(&message) ? 0 : 1;
}
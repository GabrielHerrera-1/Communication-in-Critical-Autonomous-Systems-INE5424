#include "../src/application/vehicle_protocol.h"
#include "../src/communication/communicator.h"
#include "../src/communication/message.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/mock_component.h"
#include <iostream>
#include <unistd.h>

int main(){

    Mock_Component* l = new Mock_Component("mock");

    Vehicle v1 = Vehicle();

    v1.add_component(l,0x0000);

    v1.initialize();

    v1.run();

}
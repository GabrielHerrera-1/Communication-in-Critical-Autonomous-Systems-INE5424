#include "../src/application/vehicle_protocol.h"
#include "../src/communication/communicator.h"
#include "../src/communication/message.h"
#include "../src/application/vehicle.h"
#include "../src/application/components/sensors/lidar_sensor.h"
#include <iostream>
#include <unistd.h>

int main(){

    Lidar_Sensor* l = new Lidar_Sensor("lidar");

    Vehicle v1 = Vehicle();

    v1.add_component(l);

    v1.initialize();
    
    v1.run();

    
}
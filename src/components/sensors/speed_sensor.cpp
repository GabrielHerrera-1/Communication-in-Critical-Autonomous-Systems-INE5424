#include "speed_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>

Speed_Sensor::Speed_Sensor(const std::string& id, unsigned int interval)
    : Sensor(id, interval) {}

Speed_Sensor::~Speed_Sensor() {}

void Speed_Sensor::initialize() {
    std::cout << "[" << component_id << "] Initializing Speed sensor..." << std::endl;
}

void Speed_Sensor::run() {
    // for (int i = 0; i < 5; i++) {
    //     std::cout << "[" << component_id << "] Reading speed..." << std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    // }
    // std::cout << "[" << component_id << "] Finished reading speed." << std::endl;
}

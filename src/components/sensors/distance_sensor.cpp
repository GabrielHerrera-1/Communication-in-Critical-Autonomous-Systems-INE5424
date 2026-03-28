#include "distance_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>

Distance_Sensor::Distance_Sensor(const std::string& id, unsigned int interval)
    : Sensor(id, interval) {}

Distance_Sensor::~Distance_Sensor() {}

void Distance_Sensor::initialize() {
    std::cout << "[" << component_id << "] Initializing Distance sensor..." << std::endl;
}

void Distance_Sensor::run() {
    // for (int i = 0; i < 5; i++) {
    //     std::cout << "[" << component_id << "] Reading distance..." << std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    // }
    // std::cout << "[" << component_id << "] Finished reading distance." << std::endl;
}

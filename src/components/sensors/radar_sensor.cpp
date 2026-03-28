#include "radar_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>

Radar_Sensor::Radar_Sensor(const std::string& id, unsigned int interval)
    : Sensor(id, interval) {}

Radar_Sensor::~Radar_Sensor() {}

void Radar_Sensor::initialize() {
    std::cout << "[" << component_id << "] Initializing Radar sensor..." << std::endl;
}

void Radar_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        std::cout << "[" << component_id << "] Scanning environment with Radar..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    }
    std::cout << "[" << component_id << "] Finished Radar scan." << std::endl;
}

#include "lidar_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>

Lidar_Sensor::Lidar_Sensor(const std::string& id, unsigned int interval)
    : Sensor(id, interval) {}

Lidar_Sensor::~Lidar_Sensor() {}

void Lidar_Sensor::initialize() {
    std::cout << "[" << component_id << "] Initializing Lidar sensor..." << std::endl;
}

void Lidar_Sensor::run() {
    // for (int i = 0; i < 5; i++) {
    //     std::cout << "[" << component_id << "] Scanning environment with Lidar..." << std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    // }
    // std::cout << "[" << component_id << "] Finished Lidar scan." << std::endl;
}
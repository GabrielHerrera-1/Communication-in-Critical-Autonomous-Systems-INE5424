#include "camera_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>

Camera_Sensor::Camera_Sensor(const std::string& id, unsigned int interval)
    : Sensor(id, interval) {}

Camera_Sensor::~Camera_Sensor() {}

void Camera_Sensor::initialize() {
    std::cout << "[" << component_id << "] Initializing Camera sensor..." << std::endl;
}

void Camera_Sensor::run() {
    // for (int i = 0; i < 5; i++) {
    //     std::cout << "[" << component_id << "] Capturing image..." << std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    // }
    // std::cout << "[" << component_id << "] Finished capturing images." << std::endl;
}

#include "steering_actuator.h"
#include <iostream>
#include <thread>
#include <chrono>

Steering_Actuator::Steering_Actuator(const std::string& id)
    : Actuator(id) {}

Steering_Actuator::~Steering_Actuator() {}

void Steering_Actuator::initialize() {
    std::cout << "[" << component_id << "] Steering system initialized." << std::endl;
}

void Steering_Actuator::run() {
    // std::cout << "[" << component_id << "] Starting steering control..." << std::endl;
    // for (int i = 0; i < 5; i++) {
    //     std::cout << "[" << component_id << "] Current angle: " << current_angle << " degrees" << std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // }
    // std::cout << "[" << component_id << "] Steering run finished." << std::endl;
}

void Steering_Actuator::set_target_value(double value) {
    current_angle = value;
    std::cout << "[" << component_id << "] Steering angle set to " << current_angle << " degrees" << std::endl;
}

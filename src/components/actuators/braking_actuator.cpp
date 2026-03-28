#include "braking_actuator.hpp"
#include <iostream>
#include <thread>
#include <chrono>

Braking_Actuator::Braking_Actuator(const std::string& id)
    : Actuator(id) {}

Braking_Actuator::~Braking_Actuator() {}

void Braking_Actuator::initialize() {
    std::cout << "[" << component_id << "] Braking system initialized." << std::endl;
}

void Braking_Actuator::run() {
    std::cout << "[" << component_id << "] Starting braking control..." << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "[" << component_id << "] Current brake force: " << brake_force << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "[" << component_id << "] Braking run finished." << std::endl;
}

void Braking_Actuator::set_target_value(double value) {
    brake_force = value;
    std::cout << "[" << component_id << "] Brake force set to " << brake_force << std::endl;
}

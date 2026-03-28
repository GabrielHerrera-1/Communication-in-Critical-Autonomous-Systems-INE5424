#include "powertrain_actuator.h"
#include <iostream>
#include <thread>
#include <chrono>

Powertrain_Actuator::Powertrain_Actuator(const std::string& id)
    : Actuator(id) {}

Powertrain_Actuator::~Powertrain_Actuator() {}

void Powertrain_Actuator::initialize() {
    std::cout << "[" << component_id << "] Powertrain initialized." << std::endl;
}

void Powertrain_Actuator::run() {
    // std::cout << "[" << component_id << "] Starting powertrain control..." << std::endl;
    // for (int i = 0; i < 5; i++) {
    //     std::cout << "[" << component_id << "] Current power: " << current_power << " kW" << std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // }
    // std::cout << "[" << component_id << "] Powertrain run finished." << std::endl;
}

void Powertrain_Actuator::set_target_value(double value) {
    current_power = value;
    std::cout << "[" << component_id << "] Power set to " << current_power << " kW" << std::endl;
}

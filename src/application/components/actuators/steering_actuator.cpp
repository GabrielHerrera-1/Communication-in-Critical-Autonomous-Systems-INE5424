#include "steering_actuator.h"
#include <thread>
#include <chrono>

Steering_Actuator::Steering_Actuator(const std::string& id)
    : Actuator(id) {}

Steering_Actuator::~Steering_Actuator() {}

void Steering_Actuator::initialize() {
    _angle_deg = 0.0;
}

void Steering_Actuator::run() {
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

Component::Port Steering_Actuator::logical_port() const {
    return Component_Ports::STEERING_ACTUATOR;
}

void Steering_Actuator::apply(double value) {
    if (value < -45.0) value = -45.0;
    if (value > 45.0) value = 45.0;
    _angle_deg = value;
}

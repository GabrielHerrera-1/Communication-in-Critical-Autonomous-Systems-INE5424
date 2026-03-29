#include "braking_actuator.h"
#include <thread>
#include <chrono>

Braking_Actuator::Braking_Actuator(const std::string& id)
    : Actuator(id) {}

Braking_Actuator::~Braking_Actuator() {}

void Braking_Actuator::initialize() {
    _pressure_bar = 0.0;
}

void Braking_Actuator::run() {
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}

void Braking_Actuator::apply(double value) {
    if (value < 0.0) value = 0.0;
    if (value > 150.0) value = 150.0;
    _pressure_bar = value;
}

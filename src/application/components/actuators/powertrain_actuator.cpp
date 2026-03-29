#include "powertrain_actuator.h"
#include <thread>
#include <chrono>

Powertrain_Actuator::Powertrain_Actuator(const std::string& id)
    : Actuator(id) {}

Powertrain_Actuator::~Powertrain_Actuator() {}

void Powertrain_Actuator::initialize() {
    _power_kw = 0.0;
}

void Powertrain_Actuator::run() {
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void Powertrain_Actuator::apply(double value) {
    if (value < 0.0) value = 0.0;
    _power_kw = value;
}

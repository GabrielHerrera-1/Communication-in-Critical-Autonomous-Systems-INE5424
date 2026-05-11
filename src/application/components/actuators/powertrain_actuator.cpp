#include "powertrain_actuator.h"
#include <sched.h>

Powertrain_Actuator::Powertrain_Actuator(const std::string& id)
    : Actuator(id) {}

Powertrain_Actuator::~Powertrain_Actuator() {}

void Powertrain_Actuator::initialize() {
    _power_kw = 0.0;
}

void Powertrain_Actuator::run() {
    for (int i = 0; i < 5; i++) {
        sched_yield();
    }
}

Component::Port Powertrain_Actuator::logical_port() const {
    return Component_Ports::POWERTRAIN_ACTUATOR;
}

void Powertrain_Actuator::apply(double value) {
    if (value < 0.0) value = 0.0;
    _power_kw = value;
}

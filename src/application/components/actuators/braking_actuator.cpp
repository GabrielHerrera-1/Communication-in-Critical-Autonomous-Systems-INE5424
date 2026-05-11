#include "braking_actuator.h"
#include <sched.h>

Braking_Actuator::Braking_Actuator(const std::string& id)
    : Actuator(id) {}

Braking_Actuator::~Braking_Actuator() {}

void Braking_Actuator::initialize() {
    _pressure_bar = 0.0;
}

void Braking_Actuator::run() {
    for (int i = 0; i < 5; i++) {
        sched_yield();
    }
}

Component::Port Braking_Actuator::logical_port() const {
    return Component_Ports::BRAKING_ACTUATOR;
}

void Braking_Actuator::apply(double value) {
    if (value < 0.0) value = 0.0;
    if (value > 150.0) value = 150.0;
    _pressure_bar = value;
}

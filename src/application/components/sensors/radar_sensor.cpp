#include "radar_sensor.h"
#include <cstdlib>
#include <sched.h>

Radar_Sensor::Radar_Sensor(const std::string& id, unsigned int interval_ms)
    : Sensor(id, interval_ms) {}

Radar_Sensor::~Radar_Sensor() {}

void Radar_Sensor::initialize() {
    _range_m = 0.0;
    _rel_speed = 0.0;
}

void Radar_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        _range_m = 5.0 + (rand() % 200);
        _rel_speed = (rand() % 60 - 30) * 0.5;
        sched_yield();
    }
}

Component::Port Radar_Sensor::logical_port() const {
    return Component_Ports::RADAR_SENSOR;
}

Component::RT_Profile Radar_Sensor::rt_profile() const {
    RT_Profile p;
    p.policy = RT_Profile::Policy::DEADLINE;
    return p;
}

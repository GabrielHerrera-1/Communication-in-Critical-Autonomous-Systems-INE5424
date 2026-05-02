#include "lidar_sensor.h"
#include <cstdlib>
#include <sched.h>

Lidar_Sensor::Lidar_Sensor(const std::string& id, unsigned int interval_ms)
    : Sensor(id, interval_ms) {}

Lidar_Sensor::~Lidar_Sensor() {}

void Lidar_Sensor::initialize() {
    _point_count = 0;
}

void Lidar_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        _point_count = 500 + (rand() % 1500);
        sched_yield();
    }
}

Component::Port Lidar_Sensor::logical_port() const {
    return Component_Ports::LIDAR_SENSOR;
}

Component::RT_Profile Lidar_Sensor::rt_profile() const {
    RT_Profile p;
    p.policy = RT_Profile::Policy::DEADLINE;
    return p;
}

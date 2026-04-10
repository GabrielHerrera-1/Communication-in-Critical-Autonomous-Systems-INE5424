#include "radar_sensor.h"
#include <thread>
#include <chrono>
#include <cstdlib>

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
        std::this_thread::sleep_for(std::chrono::milliseconds(_interval_ms));
    }
}

Component::Port Radar_Sensor::logical_port() const {
    return Component_Ports::RADAR_SENSOR;
}

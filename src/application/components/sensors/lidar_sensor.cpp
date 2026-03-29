#include "lidar_sensor.h"
#include <thread>
#include <chrono>
#include <cstdlib>

Lidar_Sensor::Lidar_Sensor(const std::string& id, unsigned int interval_ms)
    : Sensor(id, interval_ms) {}

Lidar_Sensor::~Lidar_Sensor() {}

void Lidar_Sensor::initialize() {
    _point_count = 0;
}

void Lidar_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        _point_count = 500 + (rand() % 1500);

        std::this_thread::sleep_for(std::chrono::milliseconds(_interval_ms));
    }
}

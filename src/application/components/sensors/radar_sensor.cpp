#include "radar_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>

Radar_Sensor::Radar_Sensor(const std::string& id, unsigned int interval_ms)
    : Sensor(id, interval_ms) {}

Radar_Sensor::~Radar_Sensor() {}

void Radar_Sensor::initialize() {
    _range_m = 0.0;
    _rel_speed = 0.0;
    std::cout << "[" << _id << "] radar calibrado." << std::endl;
}

void Radar_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        _range_m = 5.0 + (rand() % 200); // 5 a 205 metros
        _rel_speed = (rand() % 60 - 30) * 0.5; // -15 a +15 m/s
        std::cout << "[" << _id << "] alvo a " << _range_m
                  << "m, vel.rel=" << _rel_speed << " m/s" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(_interval_ms));
    }
    std::cout << "[" << _id << "] leituras radar finalizadas." << std::endl;
}

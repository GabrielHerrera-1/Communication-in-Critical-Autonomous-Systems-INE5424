#include "lidar_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include "../../../communication/message.h"

Lidar_Sensor::Lidar_Sensor(const std::string& id, unsigned int interval_ms)
    : Sensor(id, interval_ms) {}

Lidar_Sensor::~Lidar_Sensor() {}

void Lidar_Sensor::initialize() {
    _point_count = 0;
    std::cout << "[" << _id << "] lidar pronto, aguardando varreduras." << std::endl;
}

void Lidar_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        _point_count = 500 + (rand() % 1500); // simula nuvem de pontos

        std::ostringstream oss;
        oss << "[" << _id << "] varredura " << (i + 1)
        << ": " << _point_count << " pontos" << std::endl;

        std::string str = oss.str();

        Message msg((void*) str.data(),str.size());

        std::cout<< str << std::endl;

        if (_endpoint)
            _endpoint->send(&msg);

        std::this_thread::sleep_for(std::chrono::milliseconds(_interval_ms));
    }
    std::ostringstream oss;
        oss << "[" << _id << "] varreduras lidar finalizadas." << std::endl;
    std::string str = oss.str();

    Message msg((void*)str.data(),str.size());

    if (_endpoint)
        _endpoint->send(&msg);
}

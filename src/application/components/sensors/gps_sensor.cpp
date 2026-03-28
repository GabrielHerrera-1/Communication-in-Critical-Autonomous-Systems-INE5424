#include "gps_sensor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>

GPS_Sensor::GPS_Sensor(const std::string& id, unsigned int interval_ms)
    : Sensor(id, interval_ms) {}

GPS_Sensor::~GPS_Sensor() {}

void GPS_Sensor::initialize() {
    // posicao inicial arbitraria (Florianopolis)
    _latitude = -27.5954;
    _longitude = -48.5480;
    std::cout << "[" << _id << "] GPS pronto. posicao inicial: "
              << _latitude << ", " << _longitude << std::endl;
}

void GPS_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        // simula deslocamento pequeno
        _latitude += (rand() % 100 - 50) * 0.0001;
        _longitude += (rand() % 100 - 50) * 0.0001;
        std::cout << "[" << _id << "] lat=" << _latitude
                  << " lon=" << _longitude << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(_interval_ms));
    }
    std::cout << "[" << _id << "] leituras GPS finalizadas." << std::endl;
}

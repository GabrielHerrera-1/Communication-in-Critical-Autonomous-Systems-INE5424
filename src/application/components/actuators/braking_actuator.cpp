#include "braking_actuator.h"
#include <iostream>
#include <thread>
#include <chrono>

Braking_Actuator::Braking_Actuator(const std::string& id)
    : Actuator(id) {}

Braking_Actuator::~Braking_Actuator() {}

void Braking_Actuator::initialize() {
    _pressure_bar = 0.0;
    std::cout << "[" << _id << "] freios soltos." << std::endl;
}

void Braking_Actuator::run() {
    std::cout << "[" << _id << "] controle de frenagem ativo." << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "[" << _id << "] pressao: "
                  << _pressure_bar << " bar" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    std::cout << "[" << _id << "] controle de frenagem encerrado." << std::endl;
}

void Braking_Actuator::apply(double value) {
    if (value < 0.0) value = 0.0;
    if (value > 150.0) value = 150.0;
    _pressure_bar = value;
    std::cout << "[" << _id << "] pressao ajustada para "
              << _pressure_bar << " bar" << std::endl;
}

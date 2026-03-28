#include "powertrain_actuator.h"
#include <iostream>
#include <thread>
#include <chrono>

Powertrain_Actuator::Powertrain_Actuator(const std::string& id)
    : Actuator(id) {}

Powertrain_Actuator::~Powertrain_Actuator() {}

void Powertrain_Actuator::initialize() {
    _power_kw = 0.0;
    std::cout << "[" << _id << "] powertrain em idle." << std::endl;
}

void Powertrain_Actuator::run() {
    std::cout << "[" << _id << "] controle de potencia ativo." << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "[" << _id << "] potencia: "
                  << _power_kw << " kW" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "[" << _id << "] controle de potencia encerrado." << std::endl;
}

void Powertrain_Actuator::apply(double value) {
    if (value < 0.0) value = 0.0;
    _power_kw = value;
    std::cout << "[" << _id << "] potencia ajustada para "
              << _power_kw << " kW" << std::endl;
}

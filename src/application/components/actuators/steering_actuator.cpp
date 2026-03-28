#include "steering_actuator.h"
#include <iostream>
#include <thread>
#include <chrono>

Steering_Actuator::Steering_Actuator(const std::string& id)
    : Actuator(id) {}

Steering_Actuator::~Steering_Actuator() {}

void Steering_Actuator::initialize() {
    _angle_deg = 0.0;
    std::cout << "[" << _id << "] direcao centralizada." << std::endl;
}

void Steering_Actuator::run() {
    std::cout << "[" << _id << "] controle de direcao ativo." << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "[" << _id << "] angulo: "
                  << _angle_deg << " graus" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    std::cout << "[" << _id << "] controle de direcao encerrado." << std::endl;
}

void Steering_Actuator::apply(double value) {
    if (value < -45.0) value = -45.0;
    if (value > 45.0) value = 45.0;
    _angle_deg = value;
    std::cout << "[" << _id << "] angulo ajustado para "
              << _angle_deg << " graus" << std::endl;
}

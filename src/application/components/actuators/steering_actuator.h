#ifndef STEERING_ACTUATOR_H
#define STEERING_ACTUATOR_H

#include "actuator.h"

// controle do angulo de direcao das rodas
class Steering_Actuator : public Actuator {
public:
    Steering_Actuator(const std::string& id);
    ~Steering_Actuator();

    void initialize() override;
    void run() override;
    Port logical_port() const override;
    void apply(double value) override;

private:
    double _angle_deg = 0.0; // graus, negativo = esquerda
};

#endif

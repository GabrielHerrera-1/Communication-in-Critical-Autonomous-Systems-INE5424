#ifndef POWERTRAIN_ACTUATOR_H
#define POWERTRAIN_ACTUATOR_H

#include "actuator.h"

// controle de potencia do trem de forca (motor + transmissao)
class Powertrain_Actuator : public Actuator {
public:
    Powertrain_Actuator(const std::string& id);
    ~Powertrain_Actuator();

    void initialize() override;
    void run() override;
    void apply(double value) override;

private:
    double _power_kw = 0.0;
};

#endif

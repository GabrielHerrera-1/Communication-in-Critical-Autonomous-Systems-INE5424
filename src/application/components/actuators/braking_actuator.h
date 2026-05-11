#ifndef BRAKING_ACTUATOR_H
#define BRAKING_ACTUATOR_H

#include "actuator.h"

// sistema de frenagem (pressao hidraulica em bar)
class Braking_Actuator : public Actuator {
public:
    Braking_Actuator(const std::string& id);
    ~Braking_Actuator();

    void initialize() override;
    void run() override;
    Port logical_port() const override;
    void apply(double value) override;

private:
    double _pressure_bar = 0.0;
};

#endif

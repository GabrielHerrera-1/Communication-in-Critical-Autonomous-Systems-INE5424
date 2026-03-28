#ifndef POWERTRAIN_ACTUATOR_H
#define POWERTRAIN_ACTUATOR_H

#include "actuator.h"

class Powertrain_Actuator : public Actuator {
private:
    double current_power = 0.0;

public:
    Powertrain_Actuator(const std::string& id);
    ~Powertrain_Actuator() override;

    void initialize() override;
    void run() override;
    void set_target_value(double value) override;
    ComponentType get_component_type() const override { return ACTUATOR_POWERTRAIN; }
};

#endif

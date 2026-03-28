#ifndef STEERING_ACTUATOR_H
#define STEERING_ACTUATOR_H

#include "actuator.h"

class Steering_Actuator : public Actuator {
private:
    double current_angle = 0.0;

public:
    Steering_Actuator(const std::string& id);
    ~Steering_Actuator() override;

    void initialize() override;
    void run() override;
    void set_target_value(double value) override;
    ComponentType get_component_type() const override { return ACTUATOR_STEERING; }
};

#endif

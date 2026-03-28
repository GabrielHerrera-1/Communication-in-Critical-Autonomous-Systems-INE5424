#ifndef BRAKING_ACTUATOR_HPP
#define BRAKING_ACTUATOR_HPP

#include "actuator.hpp"

class Braking_Actuator : public Actuator {
private:
    double brake_force = 0.0;

public:
    Braking_Actuator(const std::string& id);
    ~Braking_Actuator() override;

    void initialize() override;
    void run() override;
    void set_target_value(double value) override;
    ComponentType get_component_type() const override { return ACTUATOR_BRAKING; }
};

#endif

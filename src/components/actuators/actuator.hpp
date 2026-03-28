#ifndef ACTUATOR_HPP
#define ACTUATOR_HPP

#include "../component.hpp"

class Actuator : public Component {
public:
    inline Actuator(const std::string& id) : Component(id) {}
    virtual ~Actuator();

    virtual void initialize() = 0;
    virtual void run() = 0;

    virtual void set_target_value(double value) = 0;
    
    // Métodos para rede
    virtual ComponentType get_component_type() const = 0;
    virtual ActuatorPayload serialize_actuator_response(double target_value, int32_t status = 0) const;
};

#endif
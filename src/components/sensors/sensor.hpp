#ifndef SENSOR_HPP
#define SENSOR_HPP

#include "../component.hpp"

class Sensor : public Component {
protected:
    unsigned int update_interval_ms = 1000;

public:
    inline Sensor(const std::string& id, unsigned int interval = 1000)
        : Component(id), update_interval_ms(interval) {}
    virtual ~Sensor();

    virtual void initialize() = 0;
    virtual void run() = 0;
    
    // Métodos para rede
    virtual ComponentType get_component_type() const = 0;
};

#endif

#ifndef SPEED_SENSOR_HPP
#define SPEED_SENSOR_HPP

#include "sensor.hpp"

class Speed_Sensor : public Sensor {
public:
    Speed_Sensor(const std::string& id, unsigned int interval = 1000);
    ~Speed_Sensor() override;

    void initialize() override;
    void run() override;
    ComponentType get_component_type() const override { return SENSOR_SPEED; }
};

#endif

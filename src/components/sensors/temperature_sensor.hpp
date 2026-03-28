#ifndef TEMPERATURE_SENSOR_HPP
#define TEMPERATURE_SENSOR_HPP

#include "sensor.hpp"

class Temperature_Sensor : public Sensor {
public:
    Temperature_Sensor(const std::string& id, unsigned int interval = 1000);
    ~Temperature_Sensor() override;

    void initialize() override;
    void run() override;
    ComponentType get_component_type() const override { return SENSOR_TEMPERATURE; }
};

#endif

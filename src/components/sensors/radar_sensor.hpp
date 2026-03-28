#ifndef RADAR_SENSOR_HPP
#define RADAR_SENSOR_HPP

#include "sensor.hpp"

class Radar_Sensor : public Sensor {
public:
    Radar_Sensor(const std::string& id, unsigned int interval = 1000);
    ~Radar_Sensor() override;

    void initialize() override;
    void run() override;
    ComponentType get_component_type() const override { return SENSOR_RADAR; }
};

#endif

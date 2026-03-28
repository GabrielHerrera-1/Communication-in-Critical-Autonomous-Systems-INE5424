#ifndef DISTANCE_SENSOR_H
#define DISTANCE_SENSOR_H

#include "sensor.h"

class Distance_Sensor : public Sensor {
public:
    Distance_Sensor(const std::string& id, unsigned int interval = 1000);
    ~Distance_Sensor() override;

    void initialize() override;
    void run() override;
    ComponentType get_component_type() const override { return SENSOR_DISTANCE; }
};

#endif

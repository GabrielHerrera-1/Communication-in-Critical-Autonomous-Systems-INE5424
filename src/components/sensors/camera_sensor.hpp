#ifndef CAMERA_SENSOR_HPP
#define CAMERA_SENSOR_HPP

#include "sensor.hpp"

class Camera_Sensor : public Sensor {
public:
    Camera_Sensor(const std::string& id, unsigned int interval = 1000);
    ~Camera_Sensor() override;

    void initialize() override;
    void run() override;
    ComponentType get_component_type() const override { return SENSOR_CAMERA; }
};

#endif

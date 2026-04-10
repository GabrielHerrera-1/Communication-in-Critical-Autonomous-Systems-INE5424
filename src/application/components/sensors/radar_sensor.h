#ifndef RADAR_SENSOR_H
#define RADAR_SENSOR_H

#include "sensor.h"

// deteccao de obstaculos por radar (distancia + velocidade relativa)
class Radar_Sensor : public Sensor {
public:
    Radar_Sensor(const std::string& id, unsigned int interval_ms = 200);
    ~Radar_Sensor();

    void initialize() override;
    void run() override;
    Port logical_port() const override;

private:
    double _range_m = 0.0;
    double _rel_speed = 0.0; // m/s, positivo = se afastando
};

#endif

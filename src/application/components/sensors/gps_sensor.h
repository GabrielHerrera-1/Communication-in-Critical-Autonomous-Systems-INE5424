#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include "sensor.h"

// simula leitura de coordenadas GPS
class GPS_Sensor : public Sensor {
public:
    GPS_Sensor(const std::string& id, unsigned int interval_ms = 1000);
    ~GPS_Sensor();

    void initialize() override;
    void run() override;
    Port logical_port() const override;
    RT_Profile rt_profile() const override;

private:
    double _latitude = 0.0;
    double _longitude = 0.0;
};

#endif

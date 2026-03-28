#ifndef LIDAR_SENSOR_H
#define LIDAR_SENSOR_H

#include "sensor.h"

// varredura 3D do ambiente ao redor do veiculo
class Lidar_Sensor : public Sensor {
public:
    Lidar_Sensor(const std::string& id, unsigned int interval_ms = 100);
    ~Lidar_Sensor();

    void initialize() override;
    void run() override;

private:
    int _point_count = 0; // pontos capturados na ultima varredura
};

#endif

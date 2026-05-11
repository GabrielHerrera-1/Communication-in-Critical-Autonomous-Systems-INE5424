#include "gps_sensor.h"
#include <cstdlib>
#include <sched.h>

GPS_Sensor::GPS_Sensor(const std::string& id, unsigned int interval_ms)
    : Sensor(id, interval_ms) {}

GPS_Sensor::~GPS_Sensor() {}

void GPS_Sensor::initialize() {
    _latitude = -27.5954;
    _longitude = -48.5480;
}

void GPS_Sensor::run() {
    for (int i = 0; i < 5; i++) {
        _latitude += (rand() % 100 - 50) * 0.0001;
        _longitude += (rand() % 100 - 50) * 0.0001;
        sched_yield();
    }
}

Component::Port GPS_Sensor::logical_port() const {
    return Component_Ports::GPS_SENSOR;
}

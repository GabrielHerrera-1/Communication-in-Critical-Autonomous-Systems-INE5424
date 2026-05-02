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
    // sob SCHED_DEADLINE, sched_yield bloqueia ate o proximo periodo (100ms).
    // o sleep_for(_interval_ms) sai porque o kernel ja garante a periodicidade
    // via runtime/period configurados no entry point do processo.
    for (int i = 0; i < 5; i++) {
        _latitude += (rand() % 100 - 50) * 0.0001;
        _longitude += (rand() % 100 - 50) * 0.0001;
        sched_yield();
    }
}

Component::Port GPS_Sensor::logical_port() const {
    return Component_Ports::GPS_SENSOR;
}

Component::RT_Profile GPS_Sensor::rt_profile() const {
    RT_Profile p;
    p.policy = RT_Profile::Policy::DEADLINE;
    return p;
}

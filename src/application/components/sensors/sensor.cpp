#include "sensor.h"

Sensor::Sensor(const std::string& id, unsigned int interval_ms)
    : Component(id), _interval_ms(interval_ms) {}

Sensor::~Sensor() {}

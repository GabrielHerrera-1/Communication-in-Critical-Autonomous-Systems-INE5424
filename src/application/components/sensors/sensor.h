#ifndef SENSOR_H
#define SENSOR_H

#include "../component.h"

// sensores leem dados do ambiente em intervalos regulares
class Sensor : public Component {
public:
    Sensor(const std::string& id, unsigned int interval_ms = 500);
    virtual ~Sensor();

protected:
    unsigned int _interval_ms;
};

#endif

#ifndef COMPONENT_PORTS_H
#define COMPONENT_PORTS_H

#include <cstdint>

class Component_Ports {
public:
    using Port = uint16_t;

    static constexpr Port GATEWAY = 0x0000;
    static constexpr Port GPS_SENSOR = 0x0101;
    static constexpr Port LIDAR_SENSOR = 0x0102;
    static constexpr Port RADAR_SENSOR = 0x0103;
    static constexpr Port BRAKING_ACTUATOR = 0x0201;
    static constexpr Port POWERTRAIN_ACTUATOR = 0x0202;
    static constexpr Port STEERING_ACTUATOR = 0x0203;

    static constexpr Port BROADCAST = 0xFFFF;
};

#endif

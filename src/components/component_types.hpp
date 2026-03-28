#ifndef COMPONENT_TYPES_HPP
#define COMPONENT_TYPES_HPP

#include <cstdint>

enum ComponentType : uint8_t {
    SENSOR_TEMPERATURE = 0x01,
    SENSOR_SPEED       = 0x02,
    SENSOR_DISTANCE    = 0x03,
    SENSOR_CAMERA      = 0x04,
    SENSOR_LIDAR       = 0x05,
    SENSOR_RADAR       = 0x06,
    ACTUATOR_BRAKING   = 0x10,
    ACTUATOR_STEERING  = 0x11,
    ACTUATOR_POWERTRAIN= 0x12,
};

// Cabe na MTU (10 bytes << 1500)
struct SensorPayload {
    ComponentType type;   // Qual componente gerou o dado
    int32_t value;        // Valor do sensor
} __attribute__((packed));

// Payload enviado pelos atuadores (confirmação de comando)
struct ActuatorPayload {
    ComponentType type;   // Qual atuador recebeu o comando
    double target_value;  // Valor alvo recebido
    int32_t status;       // Status da execução (0=ok, -1=erro)
} __attribute__((packed));

// Nomes legíveis para os logs
inline const char* component_name(ComponentType t) {
    switch (t) {
        case SENSOR_TEMPERATURE:  return "Temperature";
        case SENSOR_SPEED:        return "Speed";
        case SENSOR_DISTANCE:     return "Distance";
        case SENSOR_CAMERA:       return "Camera";
        case SENSOR_LIDAR:        return "Lidar";
        case SENSOR_RADAR:        return "Radar";
        case ACTUATOR_BRAKING:    return "Braking";
        case ACTUATOR_STEERING:   return "Steering";
        case ACTUATOR_POWERTRAIN: return "Powertrain";
        default:                  return "Unknown";
    }
}

#endif

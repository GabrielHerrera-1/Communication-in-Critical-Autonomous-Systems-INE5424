#ifndef UNIT_H
#define UNIT_H

#include <cstdint>

// Unit: o "type" das mensagens de Interesse e Resposta (spec da etapa 5)
// Identifica inequivocamente o tipo do dado
enum class Unit : uint32_t {
    NONE            = 0x00000000,

    GPS_POSITION    = 0x00000001, // posicao (lat, lon)
    SPEED           = 0x00000002, // velocidade (m/s)
    LIDAR_DISTANCE  = 0x00000003, // distancia (m)
    RADAR_DISTANCE  = 0x00000004,

    BRAKE_PRESSURE  = 0x00000010, // pressao de frenagem (bar)
    THROTTLE        = 0x00000011,
    STEERING_ANGLE  = 0x00000012,

    TEST_COUNTER    = 0x0000F001, // contador deterministico p/ testes
};

#endif

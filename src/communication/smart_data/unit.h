#ifndef UNIT_H
#define UNIT_H

#include <cstdint>

// Unit: o "type" das mensagens de Interesse e Resposta (spec da etapa 5).
// Identifica inequivocamente o tipo do dado, no espirito dos codigos TEDS
// (Transducer Electronic Data Sheet, IEEE 1451). Cada SmartData<Transducer>
// fica amarrado a uma Unit via Transducer::UNIT, e Interesse/Resposta carregam
// a Unit no cabecalho para que so os agentes capazes de produzir/consumir
// aquele tipo reajam a mensagem.
//
// Os valores sao arbitrarios mas estaveis (fazem parte do "contrato de fio"):
// dois agentes em VMs diferentes precisam concordar no codigo de cada tipo.
enum class Unit : uint32_t {
    NONE            = 0x00000000,

    // sensores
    GPS_POSITION    = 0x00000001, // posicao geografica (lat, lon)
    SPEED           = 0x00000002, // velocidade escalar (m/s)
    LIDAR_DISTANCE  = 0x00000003, // distancia do obstaculo mais proximo (m)
    RADAR_DISTANCE  = 0x00000004, // idem via radar (m)

    // atuadores (valor = setpoint atual)
    BRAKE_PRESSURE  = 0x00000010, // pressao de frenagem (bar)
    THROTTLE        = 0x00000011, // aceleracao normalizada [0,1]
    STEERING_ANGLE  = 0x00000012, // angulo de direcao (graus)

    // faixa reservada para cenarios de teste
    TEST_COUNTER    = 0x0000F001, // contador monotonico (determinismo em testes)
};

#endif

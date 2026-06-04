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

    // Reservamos uma faixa alta para cenarios de teste para nao misturar
    // trafego de validacao com IDs de componentes reais do veiculo.
    static constexpr Port TEST_MESH = 0xF001;
    static constexpr Port TEST_RTT = 0xF003;
    static constexpr Port TEST_RTT_INTRA_INITIATOR = 0xF004;
    static constexpr Port TEST_RTT_INTRA_RESPONDER = 0xF005;
    static constexpr Port TEST_STRESS_SENDER = 0xF301;
    static constexpr Port TEST_STRESS_LISTENER = 0xF302;
    static constexpr Port TEST_SPTP_DRIFT         = 0xF404;
    static constexpr Port PTP                     = 0xF407;
    static constexpr Port TEST_SPTP_SIMPLE_SENDER   = 0xF420;
    static constexpr Port TEST_SPTP_SIMPLE_RECEIVER = 0xF421;
    // Etapa 4: cenario de sincronizacao espacial por quadrantes
    static constexpr Port TEST_QUADRANT_SENDER      = 0xF430;
    static constexpr Port TEST_QUADRANT_RECEIVER    = 0xF431;
    // Etapa 5: cenarios Interesse/Resposta (publish-subscribe time-triggered)
    static constexpr Port TEST_INTEREST_PUB         = 0xF501;
    static constexpr Port TEST_INTEREST_SUB         = 0xF502;
};

#endif

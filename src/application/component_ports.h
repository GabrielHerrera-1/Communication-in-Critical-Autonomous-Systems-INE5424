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
    static constexpr Port TEST_GATEWAY_PATH = 0xF002;
    static constexpr Port TEST_RTT = 0xF003;
    static constexpr Port TEST_RTT_INTRA_INITIATOR = 0xF004;
    static constexpr Port TEST_RTT_INTRA_RESPONDER = 0xF005;
    static constexpr Port TEST_LOCAL_BROADCAST_BASIC_SENDER = 0xF101;
    static constexpr Port TEST_LOCAL_BROADCAST_BASIC_RECEIVER_A = 0xF102;
    static constexpr Port TEST_LOCAL_BROADCAST_BASIC_RECEIVER_B = 0xF103;
    static constexpr Port TEST_LOCAL_BROADCAST_A = 0xF201;
    static constexpr Port TEST_LOCAL_BROADCAST_B = 0xF202;
    static constexpr Port TEST_LOCAL_BROADCAST_C = 0xF203;
    static constexpr Port TEST_LOCAL_BROADCAST_D = 0xF204;
    static constexpr Port TEST_LOCAL_BROADCAST_E = 0xF205;
    static constexpr Port TEST_STRESS_SENDER = 0xF301;
    static constexpr Port TEST_STRESS_LISTENER = 0xF302;

    // TODO: limpar isso aqui
    static constexpr Port TEST_TIMESTAMP_SENDER   = 0xF401;
    static constexpr Port TEST_TIMESTAMP_RECEIVER = 0xF402;
    static constexpr Port TEST_SPTP_SYNC          = 0xF403;
    static constexpr Port TEST_DRIFT              = 0xF404;
    static constexpr Port TEST_SPTP_DRIFT         = 0xF404;
    static constexpr Port TEST_ANTENNA            = 0xF405;
};

#endif

#ifndef PACKET_KIND_H
#define PACKET_KIND_H

#include <cstdint>

// permite protocol distinguir mensagens normais de mensagens internas do canal (futuramente grupos tb)
// DATA --> mensagem normal da aplicacao, deve ser entregue aos componentes
// SPTP_REQUEST_SYNC / SPTP_SYNC --> mensagens internas do sptp, consumidas pelo gateway
enum class PacketKind : uint8_t {
    DATA = 0,
    SPTP_SYNC = 1,
    SPTP_REQUEST_SYNC = 2,
};

#endif
#ifndef SMART_MESSAGE_H
#define SMART_MESSAGE_H

#include <cstdint>
#include "unit.h"

// Estruturas que viajam DENTRO do payload (data()) de um TypedMessage.
//
// Pela spec:
//   I = {origin, timestamp, type, period}   // period em us a partir de agora
//   R = {origin, timestamp, type, value}
//
// origin e timestamp ja viajam no Protocol::Header e sao preenchidos pelo
// Communicator no send/receive (origin = MAC+porta+quadrante; timestamp =
// Clock::monotonic_stamp). Portanto o payload so precisa carregar:
//   - kind: a natureza da mensagem (Interesse vs Resposta) -- o "codigo capaz
//     de identificar sua natureza" exigido pela spec;
//   - unit: o type (codigo TEDS) do dado;
//   - period (Interesse) ou value (Resposta).
//
// Tudo packed: as structs cruzam a fronteira de VMs (mesma convencao das
// payloads do SPTP) e nao podem depender de padding do compilador.

struct SmartHeader {
    enum Kind : uint8_t {
        INTEREST = 0,
        RESPONSE = 1,
    };

    uint8_t kind;
    Unit    unit;
} __attribute__((packed));

// Mensagem de Interesse. period_us == periodo pedido para as respostas.
// disinterest == 1 cancela o interesse (o "bit de desinteresse" usado quando
// um veiculo sai da simulacao ou troca de quadrante).
struct InterestMessage {
    SmartHeader header;
    uint64_t    period_us;
    uint8_t     disinterest;
} __attribute__((packed));

// Mensagem de Resposta, parametrizada pelo tipo do valor produzido pelo
// Transducer. O consumidor usa o mesmo Transducer (logo o mesmo Value), entao
// le de volta corretamente. SmartHeader vem primeiro: reinterpretar os
// primeiros bytes como SmartHeader e sempre valido para descobrir kind/unit.
template <typename Value>
struct ResponseMessage {
    SmartHeader header;
    Value       value;
} __attribute__((packed));

#endif

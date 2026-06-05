#ifndef SMART_MESSAGE_H
#define SMART_MESSAGE_H

#include <cstdint>
#include "unit.h"

// Estruturas que viajam DENTRO do payload (data()) de uma TypedMessage.
//
// Pela spec:
//   I = {origin, timestamp, type, period}   // period em us a partir de agora
//   R = {origin, timestamp, type, value}
//
// origin e timestamp ja viajam no Protocol::Header (preenchidos pelo Communicator
// no send/receive). O payload so carrega:
//   - kind: a natureza (Interesse vs Resposta) -- o codigo da spec;
//   - unit: o type (codigo TEDS);
//   - period (Interesse) ou value (Resposta).
//
// Tudo packed: as structs cruzam a fronteira de VMs (sem padding do compilador).

struct SmartHeader {
    enum Kind : uint8_t { INTEREST = 0, RESPONSE = 1 };
    uint8_t kind;
    Unit    unit;
} __attribute__((packed));

struct InterestMessage {
    SmartHeader header;
    uint64_t    period_us;    // periodo pedido para as respostas
    uint8_t     disinterest;  // 1 = cancela o interesse (bit de desinteresse)
} __attribute__((packed));

template <typename Value>
struct ResponseMessage {
    SmartHeader header;
    Value       value;
} __attribute__((packed));

#endif

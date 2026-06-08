#ifndef SMART_MESSAGE_H
#define SMART_MESSAGE_H

#include <cstdint>
#include "unit.h"

// estruturas que viajam dentro do payload (data()) de uma TypedMessage

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

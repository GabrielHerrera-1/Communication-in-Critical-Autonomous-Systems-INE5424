#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <cstdint>
#include "unit.h"

// Descritores de tipo: associam um codigo TEDS (Unit) ao tipo do valor. NAO
// contem logica de producao (isso vive no componente, via IProducer). So
// parametrizam o SmartData<Tipo>.

struct Speed_Data {
    static constexpr Unit UNIT = Unit::SPEED;
    struct Value { double mps; };
};

struct Lidar_Data {
    static constexpr Unit UNIT = Unit::LIDAR_DISTANCE;
    struct Value { double meters; };
};

// tipo deterministico, conveniente para asserts de teste
struct Counter_Data {
    static constexpr Unit UNIT = Unit::TEST_COUNTER;
    struct Value { uint64_t seq; };
};

#endif

#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <cstdint>
#include "unit.h"

// Descritores de tipo de dado: associam um codigo TEDS (Unit) ao tipo do valor
// transmitido. NAO contem logica de producao -- isso e responsabilidade dos
// componentes, via IProducer<Value>. Servem apenas para parametrizar o
// SmartData<Tipo> (uma "classe por tipo de dado").

struct Speed_Data {
    static constexpr Unit UNIT = Unit::SPEED;
    struct Value { double mps; };
};

struct Lidar_Data {
    static constexpr Unit UNIT = Unit::LIDAR_DISTANCE;
    struct Value { double meters; };
};

// tipo determinístico, conveniente para asserts de teste
struct Counter_Data {
    static constexpr Unit UNIT = Unit::TEST_COUNTER;
    struct Value { uint64_t seq; };
};

#endif

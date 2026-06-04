#ifndef TRANSDUCER_H
#define TRANSDUCER_H

#include <cstdint>
#include <type_traits>
#include "unit.h"

// Conceito de Transducer (substitui o stub IProducer).
//
// Um Transducer e o objeto que "fornece o valor" no lado responsivo. Em vez de
// um ponteiro para um produtor (como no esboco antigo), o SmartData<Transducer>
// recebe um Transducer e chama sense() para gerar cada resposta. Um Transducer
// concreto define:
//   - static constexpr Unit UNIT;   // o type (codigo TEDS) que ele produz
//   - typedef ... Value;            // POD trivialmente copiavel (cruza o fio)
//   - Value sense();                // le/gera o valor atual
//   - (atuadores) void actuate(const Value&);
//
// Os valores aqui sao simulados (a aquisicao real de sensores esta fora do
// escopo da etapa 5); o que importa e o fluxo Interesse/Resposta time-triggered.

// Contador monotonico: deterministico, ideal para asserts de teste.
struct Counter_Transducer {
    static constexpr Unit UNIT = Unit::TEST_COUNTER;

    struct Value {
        uint64_t seq;
    };

    Value sense() { return Value{ ++_seq }; }

private:
    uint64_t _seq = 0;
};

// Posicao GPS simulada (passeio aleatorio em torno de um ponto fixo).
struct GPS_Transducer {
    static constexpr Unit UNIT = Unit::GPS_POSITION;

    struct Value {
        double lat;
        double lon;
    };

    Value sense() {
        _lat += (static_cast<double>(_step % 7) - 3.0) * 0.0001;
        _lon += (static_cast<double>(_step % 5) - 2.0) * 0.0001;
        ++_step;
        return Value{ _lat, _lon };
    }

private:
    double   _lat = -27.5954; // Florianopolis, como o GPS_Sensor existente
    double   _lon = -48.5480;
    uint64_t _step = 0;
};

// Velocidade escalar simulada (m/s) variando suavemente.
struct Speed_Transducer {
    static constexpr Unit UNIT = Unit::SPEED;

    struct Value {
        double mps;
    };

    Value sense() {
        _mps = 10.0 + static_cast<double>(_step % 20);
        ++_step;
        return Value{ _mps };
    }

private:
    double   _mps = 10.0;
    uint64_t _step = 0;
};

// garante que os Value cruzam o fio com seguranca (sem ponteiros/padding magico)
static_assert(std::is_trivially_copyable<Counter_Transducer::Value>::value, "Value POD");
static_assert(std::is_trivially_copyable<GPS_Transducer::Value>::value, "Value POD");
static_assert(std::is_trivially_copyable<Speed_Transducer::Value>::value, "Value POD");

#endif

#include "../src/application/vehicle.h"
#include "../src/application/components/component.h"
#include "../src/core/clock.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sched.h>
#include <sys/resource.h>
#include <sys/time.h>

// Objetivo: validar empiricamente o SCHED_DEADLINE (period=100ms, runtime=5ms,
// deadline=100ms) configurado para os componentes do veiculo.
//
// O componente abaixo:
//  1. Configura-se em SCHED_DEADLINE via rt_profile().
//  2. Em cada iteracao, le CLOCK_REALTIME e o CPU consumido via getrusage,
//     simula uma carga leve, e chama sched_yield() (que sob DEADLINE bloqueia
//     ate o proximo periodo).
//  3. No final, imprime estatisticas:
//       - intervalo real entre iteracoes (deve ficar perto de 100ms)
//       - CPU efetivamente consumido por iteracao (deve ser muito menor que
//         o runtime de 5ms para confirmar que o sistema "fica ocioso", como
//         o professor previu)
//
// Criterio de sucesso: periodo medio entre 80ms e 120ms e CPU usado por
// iteracao menor que 4ms (deixando margem ate o budget de 5ms).

namespace {

constexpr int    SAMPLES                  = 30;
constexpr int64_t EXPECTED_PERIOD_NS      = 100'000'000LL;
constexpr int64_t PERIOD_TOLERANCE_NS     = 20'000'000LL;     // +- 20%
constexpr int64_t RUNTIME_BUDGET_NS       =   5'000'000LL;
constexpr int64_t RUNTIME_HEADROOM_LIMIT  =   4'000'000LL;    // 80% do budget

int64_t cpu_time_ns() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
    int64_t user = static_cast<int64_t>(ru.ru_utime.tv_sec) * 1'000'000'000LL
                 + static_cast<int64_t>(ru.ru_utime.tv_usec) * 1'000LL;
    int64_t sys  = static_cast<int64_t>(ru.ru_stime.tv_sec) * 1'000'000'000LL
                 + static_cast<int64_t>(ru.ru_stime.tv_usec) * 1'000LL;
    return user + sys;
}

class Deadline_Tester : public Component {
public:
    Deadline_Tester() : Component("deadline-tester") {}
    void initialize() override {}

    void run() override {
        int64_t wall_ts[SAMPLES];
        int64_t cpu_ts[SAMPLES];

        for (int i = 0; i < SAMPLES; ++i) {
            wall_ts[i] = Clock::now_ns();
            cpu_ts[i]  = cpu_time_ns();

            // carga sintetica leve para garantir que o thread realmente
            // consome CPU em algum momento do periodo. ~50us em CPU moderna.
            volatile uint64_t acc = 0;
            for (int j = 0; j < 10'000; ++j) acc += static_cast<uint64_t>(j);
            (void) acc;

            sched_yield();
        }

        // estatisticas de periodo (intervalo entre wakeups)
        int64_t per_min = std::numeric_limits<int64_t>::max();
        int64_t per_max = 0;
        int64_t per_sum = 0;
        for (int i = 1; i < SAMPLES; ++i) {
            int64_t dt = wall_ts[i] - wall_ts[i-1];
            if (dt < per_min) per_min = dt;
            if (dt > per_max) per_max = dt;
            per_sum += dt;
        }
        int64_t per_avg = per_sum / (SAMPLES - 1);

        // estatisticas de CPU consumido por iteracao
        int64_t cpu_min = std::numeric_limits<int64_t>::max();
        int64_t cpu_max = 0;
        int64_t cpu_sum = 0;
        for (int i = 1; i < SAMPLES; ++i) {
            int64_t dc = cpu_ts[i] - cpu_ts[i-1];
            if (dc < cpu_min) cpu_min = dc;
            if (dc > cpu_max) cpu_max = dc;
            cpu_sum += dc;
        }
        int64_t cpu_avg = cpu_sum / (SAMPLES - 1);

        std::cout << "[deadline] amostras=" << (SAMPLES - 1) << "\n";
        std::cout << "[deadline] periodo  min=" << (per_min / 1000) << "us"
                  << "  max=" << (per_max / 1000) << "us"
                  << "  avg=" << (per_avg / 1000) << "us"
                  << "  (esperado " << (EXPECTED_PERIOD_NS / 1000) << "us)\n";
        std::cout << "[deadline] cpu/iter min=" << (cpu_min / 1000) << "us"
                  << "  max=" << (cpu_max / 1000) << "us"
                  << "  avg=" << (cpu_avg / 1000) << "us"
                  << "  (budget runtime=" << (RUNTIME_BUDGET_NS / 1000) << "us)\n";
        std::cout << "[deadline] util_avg = " << (cpu_avg * 100 / per_avg)
                  << "%  (cpu_avg / periodo_avg)\n";

        bool periodo_ok = (per_avg >= EXPECTED_PERIOD_NS - PERIOD_TOLERANCE_NS) &&
                          (per_avg <= EXPECTED_PERIOD_NS + PERIOD_TOLERANCE_NS);
        bool runtime_ok = (cpu_max <= RUNTIME_HEADROOM_LIMIT);

        if (!periodo_ok) {
            std::cout << "[deadline] FALHOU: periodo medio fora da janela "
                      << ((EXPECTED_PERIOD_NS - PERIOD_TOLERANCE_NS) / 1000) << "us-"
                      << ((EXPECTED_PERIOD_NS + PERIOD_TOLERANCE_NS) / 1000) << "us\n";
            std::exit(1);
        }
        if (!runtime_ok) {
            std::cout << "[deadline] FALHOU: cpu_max=" << (cpu_max / 1000)
                      << "us excede 80% do runtime budget de "
                      << (RUNTIME_BUDGET_NS / 1000) << "us\n";
            std::exit(1);
        }

        std::cout << "[deadline] cenario validado." << std::endl;
    }

    bool subscribe_logical_broadcast() const override { return false; }
    Port logical_port() const override { return Component_Ports::TEST_DEADLINE; }

    RT_Profile rt_profile() const override {
        RT_Profile p;
        p.policy = RT_Profile::Policy::DEADLINE;
        return p;
    }
};

} // namespace

int main() {
    Vehicle vehicle;
    vehicle.add_component(new Deadline_Tester());
    vehicle.initialize();
    vehicle.run();
    return 0;
}

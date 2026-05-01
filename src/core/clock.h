#ifndef CLOCK_H
#define CLOCK_H

// TODO: ver se n quebra com gcc 11 depois
#include <atomic>
#include <cstdint>
#include <ctime>

namespace Clock {
    
    // devolve o tempo atual total em ns
    // usamos CLOCK_REALTIME porque n queremos so medir o tempo decorrido, vms temq compartilhar uma noção de horário corrigível 
    // retorna ns totais aqui
    inline int64_t now_ns() {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1000000000ULL + static_cast<int64_t>(ts.tv_nsec);
    }

    // garante q (address, timestamp) sejam unicos mesmo se o relogio for ajustado p tras
    // pelo PTP ou se duas msgs sairem no mesmo ns
    // caso normal: o relogio avançou, ai usamos o valor atual do now_ns
    // caso atipico: o relogio foi pra tras ou deu o msm tempo do previous, ai setamos o ts como prev + 1
    inline int64_t monotonic_stamp() {
        static std::atomic<int64_t> last{0};
        int64_t now = now_ns();
        int64_t prev = last.load(std::memory_order_relaxed);
        int64_t ts;
        do {
            if (now > prev) {
                ts = now;
            } else {
                ts = prev + 1;
            }
        // TODO: esse compare exchange da erro no gcc 11?
        } while (!last.compare_exchange_weak(prev, ts, std::memory_order_relaxed));
        return ts;
    }
// detalhe: o compare_exchange_weak serve pra so gravar ts em last se ninguem mexeu em last desde a hora que eu li
// serve para, caso duas threads chamem monotonic_stamp ao mesmo tempo, so uma consegue atualizar last primeiro. dispensa uso de mutex aqui
}

#endif
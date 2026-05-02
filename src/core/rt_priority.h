#ifndef RT_PRIORITY_H
#define RT_PRIORITY_H

#include <cstdint>

namespace RT_Priority {

static const int MAIN_THREAD_PRIORITY = 98;
static const int SERVICE_THREAD_PRIORITY = 99;

// parametros do SCHED_DEADLINE (EDF + CBS no kernel linux)
// constraint do kernel: runtime_ns <= deadline_ns <= period_ns
struct Deadline_Params {
    uint64_t runtime_ns;
    uint64_t deadline_ns;
    uint64_t period_ns;
};

bool set_current_thread_fifo(int priority, const char * role);
bool set_main_thread_priority(const char * role);
bool set_service_thread_priority(const char * role);

// configura a thread atual em SCHED_DEADLINE com os parametros dados.
// retorna false se sched_setattr falhou (ENOSYS = kernel sem suporte,
// EBUSY = admission control rejeitou, EPERM = sem CAP_SYS_NICE,
// EINVAL = parametros violam runtime <= deadline <= period).
bool set_current_thread_deadline(const Deadline_Params & params, const char * role);

} // namespace RT_Priority

#endif

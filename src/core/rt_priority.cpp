#include "rt_priority.h"

#include <cerrno>
#include <cstdio>
#include <sched.h>

namespace RT_Priority {

bool set_current_thread_fifo(int priority, const char * role) {
    // estrutura que o kernel usa pra receber a prioridade. zerada pra n ter lixo
    sched_param param = {};

    // range valido de prioridade
    const int min_priority = sched_get_priority_min(SCHED_FIFO);
    const int max_priority = sched_get_priority_max(SCHED_FIFO);

    // nao suporta ou deu erro
    if (min_priority < 0 || max_priority < 0) {
        std::perror("[RT_Priority] sched_get_priority_*");
        return false;
    }

    // clipa o valor dentro do range valido
    if (priority < min_priority) {
        priority = min_priority;
    } else if (priority > max_priority) {
        priority = max_priority;
    }

    // preenche a estrutura com a prioridade final
    param.sched_priority = priority;

    // aplica o scheduler FIFO na thread atual (0 = thread chamante)
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::fprintf(stderr,
                     "[RT_Priority] aviso: nao foi possivel configurar %s com SCHED_FIFO/%d (errno=%d)\n",
                     role ? role : "thread atual",
                     priority,
                     errno);
        return false;
    }

    return true;
}

// atalho pra thread principal, chama a funcao acima com prioridade 98
bool set_main_thread_priority(const char * role) {
    return set_current_thread_fifo(MAIN_THREAD_PRIORITY, role);
}

// atalho pra thread de recepção, prioridade 99
bool set_service_thread_priority(const char * role) {
    return set_current_thread_fifo(SERVICE_THREAD_PRIORITY, role);
}

} // namespace RT_Priority

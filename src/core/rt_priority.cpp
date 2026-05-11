#include "rt_priority.h"

#include <cerrno>
#include <cstdio>
#include <sched.h>

namespace RT_Priority {

namespace {
static const int MAIN_THREAD_PRIORITY    = 98;
static const int SERVICE_THREAD_PRIORITY = 99;
}

static bool set_current_thread_fifo(int priority, const char * role) {
    sched_param param = {};

    const int min_priority = sched_get_priority_min(SCHED_FIFO);
    const int max_priority = sched_get_priority_max(SCHED_FIFO);

    if (min_priority < 0 || max_priority < 0) {
        std::perror("[RT_Priority] sched_get_priority_*");
        return false;
    }

    if (priority < min_priority) priority = min_priority;
    else if (priority > max_priority) priority = max_priority;

    param.sched_priority = priority;

    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::fprintf(stderr,
                     "[RT_Priority] aviso: nao foi possivel configurar %s com SCHED_FIFO/%d (errno=%d)\n",
                     role ? role : "thread atual", priority, errno);
        return false;
    }

    return true;
}

bool set_main_thread_priority(const char * role) {
    return set_current_thread_fifo(MAIN_THREAD_PRIORITY, role);
}

bool set_service_thread_priority(const char * role) {
    return set_current_thread_fifo(SERVICE_THREAD_PRIORITY, role);
}

} // namespace RT_Priority

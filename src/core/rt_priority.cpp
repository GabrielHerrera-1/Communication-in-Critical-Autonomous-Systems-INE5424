#include "rt_priority.h"

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace RT_Priority {

// SCHED_DEADLINE (= 6) nem sempre esta exposto no <sched.h> do toolchain
// usado pra montar o initramfs, e a glibc nao expoe sched_setattr ate
// versoes recentes. definimos a struct localmente (layout estavel desde o
// kernel 3.14) e chamamos a syscall direto pra nao depender da libc.
namespace {

constexpr int SCHED_POLICY_DEADLINE = 6;

struct Sched_Attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

#ifndef SYS_sched_setattr
#  if defined(__x86_64__)
#    define SYS_sched_setattr 314
#  elif defined(__i386__)
#    define SYS_sched_setattr 351
#  elif defined(__aarch64__)
#    define SYS_sched_setattr 274
#  elif defined(__arm__)
#    define SYS_sched_setattr 380
#  endif
#endif

} // namespace

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

bool set_current_thread_deadline(const Deadline_Params & params, const char * role) {
#ifndef SYS_sched_setattr
    std::fprintf(stderr,
                 "[RT_Priority] SYS_sched_setattr indisponivel para esta arquitetura (role=%s)\n",
                 role ? role : "thread atual");
    return false;
#else
    // valida a constraint do kernel localmente para dar mensagem mais clara
    // do que o EINVAL generico que viria da syscall.
    if (params.runtime_ns == 0 ||
        params.runtime_ns  > params.deadline_ns ||
        params.deadline_ns > params.period_ns) {
        std::fprintf(stderr,
                     "[RT_Priority] parametros DEADLINE invalidos para %s: "
                     "runtime=%llu deadline=%llu period=%llu (esperado: runtime <= deadline <= period, runtime > 0)\n",
                     role ? role : "thread atual",
                     static_cast<unsigned long long>(params.runtime_ns),
                     static_cast<unsigned long long>(params.deadline_ns),
                     static_cast<unsigned long long>(params.period_ns));
        return false;
    }

    Sched_Attr attr = {};
    attr.size = sizeof(attr);
    attr.sched_policy   = SCHED_POLICY_DEADLINE;
    attr.sched_runtime  = params.runtime_ns;
    attr.sched_deadline = params.deadline_ns;
    attr.sched_period   = params.period_ns;

    if (syscall(SYS_sched_setattr, 0, &attr, 0u) != 0) {
        const int err = errno;
        const char * hint = "";
        switch (err) {
        case EBUSY:  hint = " (admission control rejeitou: utilizacao excede o budget de RT do kernel)"; break;
        case EPERM:  hint = " (precisa CAP_SYS_NICE ou rodar como root)"; break;
        case ENOSYS: hint = " (kernel nao suporta SCHED_DEADLINE)"; break;
        case EINVAL: hint = " (parametros invalidos para o kernel)"; break;
        default:     hint = "";
        }
        std::fprintf(stderr,
                     "[RT_Priority] sched_setattr(SCHED_DEADLINE) falhou para %s: errno=%d%s "
                     "runtime=%lluns deadline=%lluns period=%lluns\n",
                     role ? role : "thread atual",
                     err,
                     hint,
                     static_cast<unsigned long long>(params.runtime_ns),
                     static_cast<unsigned long long>(params.deadline_ns),
                     static_cast<unsigned long long>(params.period_ns));
        return false;
    }

    return true;
#endif
}

} // namespace RT_Priority

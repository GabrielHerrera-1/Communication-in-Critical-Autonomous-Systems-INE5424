#include "shared_memory_engine.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

namespace {

// estrutura auxiliar pra conversar com a api de semaforos do system v
// union semun existe pra empacotar argumentos extra pra semctl() (semctl inicializa, le e remove valores do semaforo)
union semun {
    int val;
    semid_ds * buf;
    unsigned short * array;
};

} // namespace

SharedMemoryEngine::Configuration SharedMemoryEngine::_configuration = {};
bool SharedMemoryEngine::_configuration_ready = false;

// metodos pra receber os indices dos semaforos
int SharedMemoryEngine::sem_component_pending(unsigned int slot) {
    return SEM_COMPONENT_PENDING_BASE + static_cast<int>(slot);
}

unsigned int SharedMemoryEngine::semaphore_count(unsigned int component_count) {
    return SEM_COMPONENT_PENDING_BASE + component_count;
}

// chamada pelo gateway
// cria infra no kernel, inicializa regiao compartilhada e devolve os ids que os outros processos vao usar dps
SharedMemoryEngine::Context SharedMemoryEngine::create(
    const uint16_t * ports,
    unsigned int component_count
) {
    // sentinela de erro. se falhar devolve esse Context invalido
    Context context = {-1, -1};

    // valida entradas
    if (!ports || component_count == 0 || component_count >= SHM::MAX_COMPONENTS) {
        return context;
    }

    const unsigned int registered_slots = component_count + 1; // slot 0 fica reservado ao gateway

    // aqui cria o segmento de memoria compartilhada no kernel
    // IPC_CREAT | 0600 cria com permissão restrita ao dono
    context.shmid = shmget(IPC_PRIVATE, sizeof(SHM::Region), IPC_CREAT | 0600);

    // falhou
    if (context.shmid < 0) {
        std::perror("[SharedMemoryEngine] shmget");
        return context;
    }

    // cria o conjunto de semaforos system v. quantidade é mutex do ring +
    // slots livres + pendencia do gateway + 1 pendencia por componente
    context.semid = semget(
        IPC_PRIVATE,
        static_cast<int>(semaphore_count(registered_slots)),
        IPC_CREAT | 0600
    );

    // se sem falharem, a shm recem criada é removida pra n vazar recurso
    if (context.semid < 0) {
        std::perror("[SharedMemoryEngine] semget");
        shmctl(context.shmid, IPC_RMID, nullptr);
        context.shmid = -1;
        return context;
    }

    // anexa a shm ao espaço de endereçamento 
    void * raw = shmat(context.shmid, nullptr, 0);

    // se nao conseguiu anexar, remove tudo que foi criado
    if (raw == reinterpret_cast<void *>(-1)) {
        std::perror("[SharedMemoryEngine] shmat");
        semctl(context.semid, 0, IPC_RMID);
        shmctl(context.shmid, IPC_RMID, nullptr);
        context = {-1, -1};
        return context;
    }

    // converte o ponteiro para o tipo real e zera tudo pra evitar lixo de memoria
    SHM::Region * region = reinterpret_cast<SHM::Region *>(raw);
    std::memset(region, 0, sizeof(SHM::Region));

    // marca a regiao como valida
    region->magic = SHM::MAGIC;
    region->component_count = static_cast<uint16_t>(registered_slots);

    // slot 0 e reservado ao gateway; componentes ficam em 1..N
    region->components[SHM::GATEWAY_SLOT].port = 0;
    region->components[SHM::GATEWAY_SLOT].slot = SHM::GATEWAY_SLOT;
    region->components[SHM::GATEWAY_SLOT].active = 0;

    for (unsigned int i = 1; i < registered_slots; ++i) {
        region->components[i].port = ports[i - 1];
        region->components[i].slot = static_cast<uint16_t>(i);
        region->components[i].active = 0;
    }

    // inicializa seq logica do ring. cada escrita recebe um numero crescente
    region->ring.next_write_seq = 0;

    // array com os valores iniciais dos semaforos
    unsigned short values[SEM_COMPONENT_PENDING_BASE + SHM::MAX_COMPONENTS] = {};

    // mutex do ring começa liberado
    values[SEM_RING_MUTEX] = 1;
    // todos slots livres no começo
    values[SEM_FREE_SLOTS] = SHM::SLOT_COUNT;
    // gateway sem mensagens
    values[SEM_GATEWAY_PENDING] = 0;
    // bootstrap fechado ate gateway e componentes declararem readiness
    values[SEM_BOOTSTRAP_MUTEX] = 1;
    values[SEM_BOOTSTRAP_RELEASE] = 0;

    // componentes sem msgs pendentes de inicio
    for (unsigned int i = 0; i < registered_slots; ++i) {
        values[sem_component_pending(i)] = 0;
    }

    // empacota o array no formato que semctl espera pra setall
    semun arg;
    arg.array = values;
    // inicializa todos os sem com os valores acima. se falhar limpa tudo
    if (semctl(context.semid, 0, SETALL, arg) < 0) {
        std::perror("[SharedMemoryEngine] semctl(SETALL)");
        shmdt(region);
        semctl(context.semid, 0, IPC_RMID);
        shmctl(context.shmid, IPC_RMID, nullptr);
        context = {-1, -1};
        return context;
    }

    // desanexa a shm depois de inicializar
    shmdt(region);
    return context;
}

void SharedMemoryEngine::destroy(const Context & context) {
    if (context.semid >= 0) {
        semctl(context.semid, 0, IPC_RMID);
    }
    if (context.shmid >= 0) {
        shmctl(context.shmid, IPC_RMID, nullptr);
    }
}

void SharedMemoryEngine::configure(const Configuration & configuration) {
    _configuration = configuration;
    _configuration_ready = true;
}

void SharedMemoryEngine::clear_configuration() {
    _configuration = {};
    _configuration_ready = false;
}

bool SharedMemoryEngine::is_gateway_process() {
    return _configuration_ready && (_configuration.slot == SHM::GATEWAY_SLOT);
}

bool SharedMemoryEngine::wait_until_all_processes_ready() {
    if (!_configuration_ready) {
        std::fprintf(stderr, "[SharedMemoryEngine] configuration ausente no bootstrap barrier\n");
        return false;
    }

    void * raw = shmat(_configuration.context.shmid, nullptr, 0);
    if (raw == reinterpret_cast<void *>(-1)) {
        std::perror("[SharedMemoryEngine] shmat(barrier)");
        return false;
    }

    SHM::Region * region = reinterpret_cast<SHM::Region *>(raw);
    if (region->magic != SHM::MAGIC) {
        std::fprintf(stderr, "[SharedMemoryEngine] region invalida no bootstrap barrier\n");
        shmdt(region);
        return false;
    }

    auto wait_sem = [&](int sem_index) -> bool {
        struct sembuf op = {};
        op.sem_num = static_cast<unsigned short>(sem_index);
        op.sem_op = -1;
        op.sem_flg = 0;

        while (semop(_configuration.context.semid, &op, 1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("[SharedMemoryEngine] semop(wait bootstrap)");
            return false;
        }
        return true;
    };

    auto signal_sem = [&](int sem_index) -> bool {
        struct sembuf op = {};
        op.sem_num = static_cast<unsigned short>(sem_index);
        op.sem_op = 1;
        op.sem_flg = 0;

        if (semop(_configuration.context.semid, &op, 1) < 0) {
            std::perror("[SharedMemoryEngine] semop(signal bootstrap)");
            return false;
        }
        return true;
    };

    bool counted = false;
    unsigned int attempts = 0;
    static const unsigned int MAX_ATTEMPTS = 30000; // ~30s com sleep de 1ms

    while (true) {
        if (!wait_sem(SEM_BOOTSTRAP_MUTEX)) {
            shmdt(region);
            return false;
        }

        if (!counted) {
            ++region->bootstrap_ready_count;
            counted = true;
        }

        if (region->bootstrap_released) {
            if (!signal_sem(SEM_BOOTSTRAP_MUTEX)) {
                shmdt(region);
                return false;
            }
            shmdt(region);
            return true;
        }

        const bool everyone_arrived =
            (region->bootstrap_ready_count == region->component_count);

        bool receivers_ready = true;
        for (unsigned int i = 0; i < region->component_count; ++i) {
            if (!region->components[i].receiver_ready) {
                receivers_ready = false;
                break;
            }
        }

        if (everyone_arrived && receivers_ready) {
            region->bootstrap_released = 1;

            if (!signal_sem(SEM_BOOTSTRAP_MUTEX)) {
                shmdt(region);
                return false;
            }

            // A nova topologia deixa o gateway criar a SHM e forkar os
            // componentes. Para o bootstrap ficar deterministico, so soltamos
            // o run() quando todos chegaram aqui e cada receiver local ja
            // publicou que sua recepcao assincrona foi armada de verdade.
            for (unsigned int i = 0; i < region->component_count; ++i) {
                if (!signal_sem(SEM_BOOTSTRAP_RELEASE)) {
                    shmdt(region);
                    return false;
                }
            }
            break;
        }

        if (!signal_sem(SEM_BOOTSTRAP_MUTEX)) {
            shmdt(region);
            return false;
        }

        if (++attempts >= MAX_ATTEMPTS) {
            std::fprintf(stderr, "[SharedMemoryEngine] timeout no bootstrap barrier\n");
            shmdt(region);
            return false;
        }

        usleep(1000);
    }

    const bool released = wait_sem(SEM_BOOTSTRAP_RELEASE);
    shmdt(region);
    return released;
}

SharedMemoryEngine::SharedMemoryEngine()
: _context{-1, -1},
  _region(nullptr),
  _slot(SHM::INVALID_SLOT),
  _port(0),
  _gateway(false),
  _nonblocking(false),
  _next_seq(0) {}

void SharedMemoryEngine::engine_init(const char *) {
    if (!_configuration_ready) {
        std::fprintf(stderr, "[SharedMemoryEngine] configuration ausente\n");
        return;
    }

    _context = _configuration.context;
    _slot = _configuration.slot;
    _port = _configuration.port;
    _gateway = (_slot == SHM::GATEWAY_SLOT);

    if (!attach_region()) {
        return;
    }

    if (!wait_semaphore(SEM_RING_MUTEX)) {
        detach_region();
        return;
    }

    // leitor passa a enxergar apenas mensagens futuras. nao le slots antigos
    _next_seq = _region->ring.next_write_seq;

    if (_gateway) {
        _region->gateway_active = 1;
        _region->components[SHM::GATEWAY_SLOT].receiver_ready = 0;
    } else if (_slot < _region->component_count) {
        _region->components[_slot].active = 1;
        _region->components[_slot].receiver_ready = 0;
    }

    signal_semaphore(SEM_RING_MUTEX);
}

int SharedMemoryEngine::engine_send(const void * frame, unsigned int size) {
    if (!_region || !frame || size == 0) {
        return -1;
    }

    // no caso de ser o gateway
    if (_gateway) {
        return write_slot(
            frame,
            size,
            SHM::GATEWAY_WRITER,
            SHM::DELIVER_TO_COMPONENTS | SHM::FROM_NETWORK
        );
    }

    // não é o gateway
    return write_slot(
        frame,
        size,
        _slot,
        SHM::DELIVER_TO_COMPONENTS | SHM::DELIVER_TO_GATEWAY
    );
}

int SharedMemoryEngine::engine_receive(void * frame, unsigned int size) {
    if (!_region || !frame || size == 0) {
        return -1;
    }
    return read_slot(frame, size);
}

void SharedMemoryEngine::engine_close() {
    // para a thread de recepção
    _running_receiver = false;
    // acorda a thread se estiver bloqueada no P() do semaforo pendente
    if (_context.semid >= 0) {
        signal_semaphore(pending_semaphore());
    }
    if (_worker.joinable()) _worker.join();

    if (_region && wait_semaphore(SEM_RING_MUTEX)) {
        if (_gateway) {
            _region->gateway_active = 0;
            _region->components[SHM::GATEWAY_SLOT].receiver_ready = 0;
        } else if (_slot < _region->component_count) {
            _region->components[_slot].active = 0;
            _region->components[_slot].receiver_ready = 0;
        }
        signal_semaphore(SEM_RING_MUTEX);
    }
    detach_region();
}

void SharedMemoryEngine::start_receiving() {
    if (!_region) return;
    _running_receiver = true;

    _worker = std::thread([this]() {
        unsigned char frame[SHM::FRAME_SIZE];

        if (!set_receiver_ready(true)) {
            _running_receiver = false;
            return;
        }

        while (_running_receiver) {
            // bloqueante: espera V() no semaforo pendente (o signal)
            int bytes = engine_receive(frame, sizeof(frame));
            if (bytes <= 0) {
                if (!_running_receiver) break;
                continue;
            }

            if (_on_receive) {
                _on_receive(reinterpret_cast<const unsigned char*>(frame), static_cast<size_t>(bytes));
            }

            // drena todos os frames pendentes (non-blocking)
            engine_set_nonblocking(true);
            while (_running_receiver) {
                bytes = engine_receive(frame, sizeof(frame));
                if (bytes <= 0) break;
                if (_on_receive) {
                    _on_receive(reinterpret_cast<const unsigned char*>(frame), static_cast<size_t>(bytes));
                }
            }
            engine_set_nonblocking(false);
        }
    });
}

bool SharedMemoryEngine::set_receiver_ready(bool ready) {
    if (!_region) {
        return false;
    }

    if (!wait_semaphore(SEM_RING_MUTEX)) {
        return false;
    }

    if (_slot < _region->component_count) {
        _region->components[_slot].receiver_ready = ready ? 1 : 0;
    }

    return signal_semaphore(SEM_RING_MUTEX);
}

void SharedMemoryEngine::engine_get_address(unsigned char * mac) {
    if (!mac) {
        return;
    }

    // na SHM o endereco fisico e interno ao veiculo.
    std::memset(mac, 0, Ethernet::Address::LENGTH);
}

void SharedMemoryEngine::engine_set_nonblocking(bool enabled) {
    _nonblocking = enabled;
}

bool SharedMemoryEngine::engine_should_drop_frame(const Ethernet::Frame &,
                                                  const Ethernet::Address &) const {
    return false;
}

// tenta anexar a regiao compartilhada ao processo atual
bool SharedMemoryEngine::attach_region() {
    void * raw = shmat(_context.shmid, nullptr, 0);
    // checa falha
    if (raw == reinterpret_cast<void *>(-1)) {
        std::perror("[SharedMemoryEngine] shmat");
        return false;
    }

    // se deu certo converte pra SHM::Region *
    _region = reinterpret_cast<SHM::Region *>(raw);

    // ve se deu certo
    if (_region->magic != SHM::MAGIC) {
        std::fprintf(stderr, "[SharedMemoryEngine] region invalida\n");
        shmdt(_region);
        _region = nullptr;
        return false;
    }

    return true;
}

void SharedMemoryEngine::detach_region() {
    if (_region) {
        shmdt(_region);
        _region = nullptr;
    }
}

// retorna o indice do sem de mensagens pendentes usado pela engine
int SharedMemoryEngine::pending_semaphore() const {
    return _gateway ? SEM_GATEWAY_PENDING : sem_component_pending(_slot);
}

bool SharedMemoryEngine::is_component_active(unsigned int slot) const {
    return _region &&
           slot < _region->component_count &&
           _region->components[slot].active;
}

bool SharedMemoryEngine::is_gateway_active() const {
    return _region && _region->gateway_active;
}

unsigned int SharedMemoryEngine::active_reader_count() const {
    unsigned int count = is_gateway_active() ? 1u : 0u;
    for (unsigned int i = 0; _region && i < _region->component_count; ++i) {
        if (is_component_active(i)) {
            ++count;
        }
    }
    return count;
}

unsigned int SharedMemoryEngine::delivery_count_for_component_write() const {
    return active_reader_count();
}

unsigned int SharedMemoryEngine::delivery_count_for_gateway_write() const {
    return active_reader_count();
}

// decide se um slot que acabou de ser consumido do ring deve ser entregue para essa instancia ou se deve ser descartado localmente
bool SharedMemoryEngine::should_deliver_slot(const SHM::Broadcast_Slot & slot) const {
    if (_gateway) {
        return slot.flags & SHM::DELIVER_TO_GATEWAY;
    }

    return (slot.flags & SHM::DELIVER_TO_COMPONENTS) && (slot.writer_slot != _slot);
}

bool SharedMemoryEngine::wait_semaphore(int sem_index) {
    struct sembuf op = {};
    op.sem_num = static_cast<unsigned short>(sem_index);
    op.sem_op = -1;
    op.sem_flg = 0;

    while (semop(_context.semid, &op, 1) < 0) {
        if (errno == EINTR) {
            continue;
        }
        std::perror("[SharedMemoryEngine] semop(wait)");
        return false;
    }

    return true;
}

bool SharedMemoryEngine::try_wait_semaphore(int sem_index) {
    struct sembuf op = {};
    op.sem_num = static_cast<unsigned short>(sem_index);
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT;

    return semop(_context.semid, &op, 1) == 0;
}

bool SharedMemoryEngine::signal_semaphore(int sem_index) {
    struct sembuf op = {};
    op.sem_num = static_cast<unsigned short>(sem_index);
    op.sem_op = 1;
    op.sem_flg = 0;

    if (semop(_context.semid, &op, 1) < 0) {
        std::perror("[SharedMemoryEngine] semop(signal)");
        return false;
    }

    return true;
}

int SharedMemoryEngine::write_slot(
    const void * frame,
    unsigned int size,
    uint16_t writer_slot,
    uint16_t flags
) {
    if (!wait_semaphore(SEM_FREE_SLOTS)) {
        return -1;
    }

    if (!wait_semaphore(SEM_RING_MUTEX)) {
        signal_semaphore(SEM_FREE_SLOTS);
        return -1;
    }

    const uint64_t seq = _region->ring.next_write_seq++;
    SHM::Broadcast_Slot & slot = _region->ring.slots[seq % SHM::SLOT_COUNT];

    slot.seq = seq;
    slot.writer_slot = writer_slot;
    slot.flags = flags;
    slot.frame_size = (size <= SHM::FRAME_SIZE) ? size : SHM::FRAME_SIZE;
    std::memcpy(slot.frame, frame, slot.frame_size);

    slot.remaining_readers = static_cast<uint16_t>(
        _gateway ? delivery_count_for_gateway_write()
                 : delivery_count_for_component_write()
    );

    if (is_gateway_active() && slot.remaining_readers > 0) {
        signal_semaphore(SEM_GATEWAY_PENDING);
    }

    for (unsigned int i = 0; i < _region->component_count; ++i) {
        if (is_component_active(i)) {
            signal_semaphore(sem_component_pending(i));
        }
    }

    if (slot.remaining_readers == 0) {
        signal_semaphore(SEM_FREE_SLOTS);
    }

    signal_semaphore(SEM_RING_MUTEX);
    return static_cast<int>(slot.frame_size);
}

int SharedMemoryEngine::read_slot(void * frame, unsigned int size) {
    const int sem = pending_semaphore();

    while (true) {
        if (_nonblocking) {
            if (!try_wait_semaphore(sem)) {
                return 0;
            }
        } else {
            if (!wait_semaphore(sem)) {
                return -1;
            }
        }

        if (!wait_semaphore(SEM_RING_MUTEX)) {
            return -1;
        }

        SHM::Broadcast_Slot & slot = _region->ring.slots[_next_seq % SHM::SLOT_COUNT];
        if (slot.seq != _next_seq) {
            signal_semaphore(SEM_RING_MUTEX);
            return -1;
        }

        const bool deliver = should_deliver_slot(slot);
        unsigned int copy_size = slot.frame_size;
        if (deliver) {
            if (copy_size > size) {
                copy_size = size;
            }

            std::memcpy(frame, slot.frame, copy_size);
        }
        ++_next_seq;

        if (slot.remaining_readers > 0) {
            --slot.remaining_readers;
            if (slot.remaining_readers == 0) {
                signal_semaphore(SEM_FREE_SLOTS);
            }
        }

        signal_semaphore(SEM_RING_MUTEX);

        if (deliver) {
            return static_cast<int>(copy_size);
        }
    }
}

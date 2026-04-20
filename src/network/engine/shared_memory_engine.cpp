#include "shared_memory_engine.h"
#include "../../core/rt_priority.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

namespace {

union semun {
    int val;
    semid_ds * buf;
    unsigned short * array;
};

} // namespace

SharedMemoryEngine::Configuration SharedMemoryEngine::_configuration = {};
bool SharedMemoryEngine::_configuration_ready = false;

int SharedMemoryEngine::pending_sem_index(uint16_t slot) {
    return SEM_PENDING_BASE + static_cast<int>(slot);
}

unsigned int SharedMemoryEngine::semaphore_count(unsigned int registered_slots) {
    return SEM_PENDING_BASE + registered_slots;
}

SharedMemoryEngine::Context SharedMemoryEngine::create(
    const uint16_t * ports,
    unsigned int component_count
) {
    Context context = {-1, -1};

    // rsu (antena) nao registra componentes: ports=nullptr e count=0.
    // validamos ports apenas quando count>0.
    if (component_count >= SHM::MAX_COMPONENTS || (component_count > 0 && !ports)) {
        return context;
    }

    const unsigned int registered_slots = component_count + 1;

    context.shmid = shmget(IPC_PRIVATE, sizeof(SHM::Region), IPC_CREAT | 0600);
    if (context.shmid < 0) {
        std::perror("[SharedMemoryEngine] shmget");
        return context;
    }

    context.semid = semget(
        IPC_PRIVATE,
        static_cast<int>(semaphore_count(registered_slots)),
        IPC_CREAT | 0600
    );
    if (context.semid < 0) {
        std::perror("[SharedMemoryEngine] semget");
        shmctl(context.shmid, IPC_RMID, nullptr);
        context.shmid = -1;
        return context;
    }

    void * raw = shmat(context.shmid, nullptr, 0);
    if (raw == reinterpret_cast<void *>(-1)) {
        std::perror("[SharedMemoryEngine] shmat");
        semctl(context.semid, 0, IPC_RMID);
        shmctl(context.shmid, IPC_RMID, nullptr);
        return {-1, -1};
    }

    SHM::Region * region = reinterpret_cast<SHM::Region *>(raw);
    std::memset(region, 0, sizeof(SHM::Region));
    region->magic = SHM::MAGIC;
    region->component_count = static_cast<uint16_t>(registered_slots);

    region->components[SHM::GATEWAY_SLOT].port = 0;
    region->components[SHM::GATEWAY_SLOT].slot = SHM::GATEWAY_SLOT;
    for (unsigned int i = 1; i < registered_slots; ++i) {
        region->components[i].port = ports[i - 1];
        region->components[i].slot = static_cast<uint16_t>(i);
    }

    shmdt(region);

    unsigned short values[SEM_PENDING_BASE + SHM::MAX_COMPONENTS] = {};
    values[SEM_RING_MUTEX] = 1;

    semun arg;
    arg.array = values;
    if (semctl(context.semid, 0, SETALL, arg) < 0) {
        std::perror("[SharedMemoryEngine] semctl(SETALL)");
        semctl(context.semid, 0, IPC_RMID);
        shmctl(context.shmid, IPC_RMID, nullptr);
        return {-1, -1};
    }

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

    if (!attach_region()) return;

    if (!sem_wait(SEM_RING_MUTEX)) {
        detach_region();
        return;
    }

    // leitor so enxerga mensagens publicadas a partir deste momento.
    _next_seq = _region->ring.next_write_seq;

    if (_gateway) {
        _region->gateway_active = 1;
    } else if (_slot < _region->component_count) {
        _region->components[_slot].active = 1;
    }
    if (_slot < _region->component_count) {
        _region->components[_slot].read_seq = _next_seq;
    }

    sem_post(SEM_RING_MUTEX);
}

int SharedMemoryEngine::engine_send(const void * frame, unsigned int size) {
    if (!_region || !frame || size == 0) return -1;

    if (_gateway) {
        return write_slot(
            frame,
            size,
            SHM::GATEWAY_WRITER,
            SHM::DELIVER_TO_COMPONENTS | SHM::FROM_NETWORK
        );
    }

    return write_slot(
        frame,
        size,
        _slot,
        SHM::DELIVER_TO_COMPONENTS | SHM::DELIVER_TO_GATEWAY
    );
}

int SharedMemoryEngine::engine_receive(void * frame, unsigned int size) {
    if (!_region || !frame || size == 0) return -1;
    return read_slot(frame, size);
}

void SharedMemoryEngine::engine_close() {
    _running_receiver = false;
    // acorda a thread se estiver bloqueada em sem_wait do proprio pending
    if (_context.semid >= 0) {
        sem_post(pending_sem_index(_slot));
    }
    if (_worker.joinable()) _worker.join();

    if (_region && sem_wait(SEM_RING_MUTEX)) {
        if (_gateway) {
            _region->gateway_active = 0;
        } else if (_slot < _region->component_count) {
            _region->components[_slot].active = 0;
        }
        sem_post(SEM_RING_MUTEX);
    }
    detach_region();
}

void SharedMemoryEngine::start_receiving() {
    if (!_region) return;
    _running_receiver = true;

    _worker = std::thread([this]() {
        RT_Priority::set_service_thread_priority("shm-recv");

        unsigned char frame[SHM::FRAME_SIZE];

        while (_running_receiver) {
            int bytes = engine_receive(frame, sizeof(frame));
            if (bytes <= 0) {
                if (!_running_receiver) break;
                continue;
            }

            if (_on_receive) {
                _on_receive(reinterpret_cast<const unsigned char*>(frame),
                            static_cast<size_t>(bytes));
            }

            engine_set_nonblocking(true);
            while (_running_receiver) {
                bytes = engine_receive(frame, sizeof(frame));
                if (bytes <= 0) break;
                if (_on_receive) {
                    _on_receive(reinterpret_cast<const unsigned char*>(frame),
                                static_cast<size_t>(bytes));
                }
            }
            engine_set_nonblocking(false);
        }
    });
}

void SharedMemoryEngine::engine_get_address(unsigned char * mac) {
    if (!mac) return;
    // na SHM o endereco fisico e interno ao veiculo.
    std::memset(mac, 0, Ethernet::Address::LENGTH);
}

void SharedMemoryEngine::engine_set_nonblocking(bool enabled) {
    _nonblocking = enabled;
}

bool SharedMemoryEngine::engine_should_drop_frame(const Ethernet::Frame &,
                                                  const Ethernet::Address &) const {
    // self-drop e feito em slot_targets_me
    return false;
}

bool SharedMemoryEngine::attach_region() {
    void * raw = shmat(_context.shmid, nullptr, 0);
    if (raw == reinterpret_cast<void *>(-1)) {
        std::perror("[SharedMemoryEngine] shmat");
        return false;
    }

    _region = reinterpret_cast<SHM::Region *>(raw);
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

// metodos locked pq presume que quem chama ta sob posse do mutex

bool SharedMemoryEngine::is_component_active_locked(unsigned int slot) const {
    return _region &&
           slot < _region->component_count &&
           _region->components[slot].active;
}

bool SharedMemoryEngine::is_gateway_active_locked() const {
    return _region && _region->gateway_active;
}

// menor read_seq entre os leitores ativos. se ninguem ativo, trata o ring como vazio pra todos (= next_write_seq, ou seja, cabe tudo)
uint64_t SharedMemoryEngine::min_active_read_seq_locked() const {
    uint64_t minv = _region->ring.next_write_seq;
    bool any = false;

    if (is_gateway_active_locked()) {
        minv = std::min(minv, _region->components[SHM::GATEWAY_SLOT].read_seq);
        any = true;
    }
    for (unsigned int i = 1; i < _region->component_count; ++i) {
        if (is_component_active_locked(i)) {
            minv = std::min(minv, _region->components[i].read_seq);
            any = true;
        }
    }
    return any ? minv : _region->ring.next_write_seq;
}

bool SharedMemoryEngine::ring_has_space_locked() const {
    return (_region->ring.next_write_seq - min_active_read_seq_locked()) < SHM::SLOT_COUNT;
}

// writer n tem como saber no momento da escrita quais leitores vao receber o frame, so sabe qm ta atiov
bool SharedMemoryEngine::should_signal_reader_locked(unsigned int reader_slot,
                                                     uint16_t writer_slot,
                                                     uint16_t flags) const {
    (void) writer_slot;
    (void) flags;
    // todo leitor precisa avanças quando chega frame, independentemente das outras flags
    if (reader_slot == SHM::GATEWAY_SLOT) {
        return is_gateway_active_locked();
    }
    return is_component_active_locked(reader_slot);
}

// leitor aplica o self drop aq
bool SharedMemoryEngine::slot_targets_me(const SHM::Broadcast_Slot & slot) const {
    if (_gateway) {
        return slot.flags & SHM::DELIVER_TO_GATEWAY;
    }
    return (slot.flags & SHM::DELIVER_TO_COMPONENTS) && (slot.writer_slot != _slot);
}

bool SharedMemoryEngine::sem_wait(int sem_index) {
    struct sembuf op = {};
    op.sem_num = static_cast<unsigned short>(sem_index);
    op.sem_op  = -1;
    op.sem_flg = 0;

    while (semop(_context.semid, &op, 1) < 0) {
        if (errno == EINTR) continue;
        std::perror("[SharedMemoryEngine] semop(wait)");
        return false;
    }
    return true;
}

// se o sem estiver em 0, em vez de dormir ele retorna o erro EAGAIN e func devolve false. usado na drenagem
bool SharedMemoryEngine::try_sem_wait(int sem_index) {
    struct sembuf op = {};
    op.sem_num = static_cast<unsigned short>(sem_index);
    op.sem_op  = -1;
    op.sem_flg = IPC_NOWAIT;

    return semop(_context.semid, &op, 1) == 0;
}

bool SharedMemoryEngine::sem_post(int sem_index) {
    struct sembuf op = {};
    op.sem_num = static_cast<unsigned short>(sem_index);
    op.sem_op  = 1;
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
    while (true) {
        if (!sem_wait(SEM_RING_MUTEX)) return -1;
        if (ring_has_space_locked()) break;
        sem_post(SEM_RING_MUTEX);
        usleep(100);
    }

    const uint64_t seq = _region->ring.next_write_seq++;
    SHM::Broadcast_Slot & slot = _region->ring.slots[seq % SHM::SLOT_COUNT];

    slot.seq         = seq;
    slot.writer_slot = writer_slot;
    slot.flags       = flags;
    slot.frame_size  = (size <= SHM::FRAME_SIZE) ? size : SHM::FRAME_SIZE;
    std::memcpy(slot.frame, frame, slot.frame_size);

    for (unsigned int i = 0; i < _region->component_count; ++i) {
        if (should_signal_reader_locked(i, writer_slot, flags)) {
            sem_post(pending_sem_index(static_cast<uint16_t>(i)));
        }
    }

    sem_post(SEM_RING_MUTEX);
    return static_cast<int>(slot.frame_size);
}

int SharedMemoryEngine::read_slot(void * frame, unsigned int size) {
    const int sem = pending_sem_index(_slot);

    while (true) {
        if (_nonblocking) {
            if (!try_sem_wait(sem)) return 0;
        } else {
            if (!sem_wait(sem)) return -1;
        }

        if (!_running_receiver) return -1;

        if (!sem_wait(SEM_RING_MUTEX)) return -1;

        SHM::Broadcast_Slot & slot = _region->ring.slots[_next_seq % SHM::SLOT_COUNT];
        if (slot.seq != _next_seq) {
            // back-pressure quebrou
            sem_post(SEM_RING_MUTEX);
            return -1;
        }

        unsigned int copy_size = 0;
        const bool deliver = slot_targets_me(slot);
        if (deliver) {
            copy_size = (slot.frame_size <= size) ? slot.frame_size : size;
            std::memcpy(frame, slot.frame, copy_size);
        }

        ++_next_seq;
        _region->components[_slot].read_seq = _next_seq;

        sem_post(SEM_RING_MUTEX);

        if (deliver) return static_cast<int>(copy_size);
        // slot nao era pra mim: avancei o cursor e volto pro proximo sem_wait
    }
}

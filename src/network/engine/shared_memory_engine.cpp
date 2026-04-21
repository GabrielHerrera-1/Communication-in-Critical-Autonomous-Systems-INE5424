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

#include <iostream>

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

    if (!ports || component_count == 0 || component_count >= SHM::MAX_COMPONENTS) {
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
    region->ring.next_write_seq = 0;

    region->components[SHM::GATEWAY_SLOT].port = 0;
    region->components[SHM::GATEWAY_SLOT].slot = SHM::GATEWAY_SLOT;
    for (unsigned int i = 1; i < registered_slots; ++i) {
        region->components[i].port = ports[i - 1];
        region->components[i].slot = static_cast<uint16_t>(i);
        region->components[i].read_seq = 0;
    }

    shmdt(region);

    unsigned short values[SEM_PENDING_BASE + SHM::MAX_COMPONENTS] = {};
    values[SEM_RING_MUTEX] = 1;
    values[SEM_RING_EMPTY] = SHM::SLOT_COUNT;

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
  _nonblocking(false){}

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

    if (_gateway) {
        _region->gateway_active = 1;
    } 
    if (_slot < _region->component_count) {
        _region->components[_slot].active = 1;
    }
    if (_slot < _region->component_count) {
        _region->components[_slot].read_seq = _region->ring.next_write_seq;
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
            SHM::DELIVER_TO_COMPONENTS
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

    sem_wait(pending_sem_index(_slot));
    if (_running_receiver){
       return read_slot(frame, size);
    }
    return -1;

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
            if (bytes <= 0) break;

            if (_on_receive) {
                _on_receive(reinterpret_cast<const unsigned char*>(frame),
                            static_cast<size_t>(bytes));
            }

        }
    });
}

void SharedMemoryEngine::engine_get_address(unsigned char * mac) {
    if (!mac) return;
    // na SHM o endereco fisico e interno ao veiculo.
    std::memset(mac, 0, Ethernet::Address::LENGTH);
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

int SharedMemoryEngine::write_slot(const void * frame, unsigned int size, uint16_t writer_slot, uint16_t flags) {

    sem_wait(SEM_RING_EMPTY);
    sem_wait(SEM_RING_MUTEX);

    const uint64_t seq = _region->ring.next_write_seq++;
    SHM::Broadcast_Slot & slot = _region->ring.slots[seq % SHM::SLOT_COUNT];

    slot.writer_slot  = writer_slot;
    slot.flags        = flags;
    slot.frame_size   = (size <= SHM::FRAME_SIZE) ? size : SHM::FRAME_SIZE;
    slot.readers_left = _region->component_count;
    std::memcpy(slot.frame, frame, slot.frame_size);

    for (unsigned int i = 0; i < _region->component_count; ++i) {
        sem_post(pending_sem_index(static_cast<uint16_t>(i)));
    }
    //std::cout << "slot " << _slot << " write seq: "<< seq << ", readers left: " << slot.readers_left << std::endl;

    sem_post(SEM_RING_MUTEX);
    return static_cast<int>(slot.frame_size);
}

int SharedMemoryEngine::read_slot(void * frame, unsigned int size) {
    sem_wait(SEM_RING_MUTEX);

    auto seq = _region->components[_slot].read_seq++;

    SHM::Broadcast_Slot & slot = _region->ring.slots[seq % SHM::SLOT_COUNT];
    
    bool deliver_to_components = slot.flags & SHM::DELIVER_TO_COMPONENTS;
    bool deliver_to_gateway = slot.flags & SHM::DELIVER_TO_GATEWAY;
    bool should_read = ((is_gateway_process() && deliver_to_gateway) || deliver_to_components);
    
    unsigned int copy_size = 0;

    if (should_read){
        copy_size = (slot.frame_size <= size) ? slot.frame_size : size;
        std::memcpy(frame, slot.frame, copy_size);
    }
    
    if(slot.readers_left <= 1){
        sem_post(SEM_RING_EMPTY);
        //std::cout << "post empty" << std::endl;
    }
    slot.readers_left--;
    //std::cout << "slot " << _slot << " read seq: "<< seq << ", readers left: " << slot.readers_left << std::endl;

    sem_post(SEM_RING_MUTEX);

    return copy_size;
    
}

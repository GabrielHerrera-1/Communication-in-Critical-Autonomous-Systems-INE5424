#include "shared_memory_engine.h"

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

union semun {
    int val;
    semid_ds * buf;
    unsigned short * array;
};

Ethernet::Address make_component_address(uint16_t port) {
    return Ethernet::Address(0x02, 0x53, 0x4d,
                             static_cast<uint8_t>((port >> 8) & 0xff),
                             static_cast<uint8_t>(port & 0xff),
                             0x01);
}

} // namespace

SharedMemoryEngine::Context SharedMemoryEngine::create(const std::vector<uint16_t> & component_ports) {
    Context context = {-1, -1, -1, -1};

    if (component_ports.empty() || component_ports.size() > MAX_COMPONENTS) {
        std::fprintf(stderr, "[SharedMemoryEngine] quantidade de componentes invalida\n");
        return context;
    }

    context.shmid = shmget(IPC_PRIVATE, sizeof(Layout), IPC_CREAT | 0600);
    if (context.shmid < 0) {
        std::perror("[SharedMemoryEngine] shmget");
        return context;
    }

    context.semid = semget(IPC_PRIVATE, static_cast<int>(component_ports.size() * 4), IPC_CREAT | 0600);
    if (context.semid < 0) {
        std::perror("[SharedMemoryEngine] semget");
        shmctl(context.shmid, IPC_RMID, nullptr);
        context.shmid = -1;
        return context;
    }

    int wakeup_pipe[2] = {-1, -1};
    if (pipe(wakeup_pipe) < 0) {
        std::perror("[SharedMemoryEngine] pipe");
        semctl(context.semid, 0, IPC_RMID);
        shmctl(context.shmid, IPC_RMID, nullptr);
        context.shmid = -1;
        context.semid = -1;
        return context;
    }

    int read_flags = fcntl(wakeup_pipe[0], F_GETFL, 0);
    int write_flags = fcntl(wakeup_pipe[1], F_GETFL, 0);
    if (read_flags >= 0) {
        fcntl(wakeup_pipe[0], F_SETFL, read_flags | O_NONBLOCK);
    }
    if (write_flags >= 0) {
        fcntl(wakeup_pipe[1], F_SETFL, write_flags | O_NONBLOCK);
    }

    void * raw = shmat(context.shmid, nullptr, 0);
    if (raw == reinterpret_cast<void *>(-1)) {
        std::perror("[SharedMemoryEngine] shmat");
        close(wakeup_pipe[0]);
        close(wakeup_pipe[1]);
        semctl(context.semid, 0, IPC_RMID);
        shmctl(context.shmid, IPC_RMID, nullptr);
        context.shmid = -1;
        context.semid = -1;
        return context;
    }

    Layout * layout = reinterpret_cast<Layout *>(raw);
    std::memset(layout, 0, sizeof(Layout));
    layout->magic = MAGIC;
    layout->component_count = component_ports.size();
    layout->gateway_address = gateway_address();
    for (unsigned int i = 0; i < component_ports.size(); ++i) {
        layout->component_ports[i] = component_ports[i];
        layout->component_addresses[i] = component_address(component_ports[i]);
    }

    std::vector<unsigned short> sem_values(component_ports.size() * 4, 0);
    for (unsigned int i = 0; i < component_ports.size(); ++i) {
        sem_values[sem_to_gateway_empty(i)] = QUEUE_LENGTH;
        sem_values[sem_to_gateway_full(i)] = 0;
        sem_values[sem_to_component_empty(i)] = QUEUE_LENGTH;
        sem_values[sem_to_component_full(i)] = 0;
    }

    semun arg;
    arg.array = sem_values.data();
    if (semctl(context.semid, 0, SETALL, arg) < 0) {
        std::perror("[SharedMemoryEngine] semctl(SETALL)");
        shmdt(layout);
        close(wakeup_pipe[0]);
        close(wakeup_pipe[1]);
        semctl(context.semid, 0, IPC_RMID);
        shmctl(context.shmid, IPC_RMID, nullptr);
        context.shmid = -1;
        context.semid = -1;
        return context;
    }

    shmdt(layout);

    context.notify_read_fd = wakeup_pipe[0];
    context.notify_write_fd = wakeup_pipe[1];
    return context;
}

void SharedMemoryEngine::destroy(const Context & context) {
    if (context.notify_read_fd >= 0) {
        close(context.notify_read_fd);
    }
    if (context.notify_write_fd >= 0) {
        close(context.notify_write_fd);
    }
    if (context.semid >= 0) {
        semctl(context.semid, 0, IPC_RMID);
    }
    if (context.shmid >= 0) {
        shmctl(context.shmid, IPC_RMID, nullptr);
    }
}

Ethernet::Address SharedMemoryEngine::gateway_address() {
    return Ethernet::Address(0x02, 0x53, 0x4d, 0x00, 0x00, 0x00);
}

Ethernet::Address SharedMemoryEngine::component_address(uint16_t port) {
    return make_component_address(port);
}

SharedMemoryEngine::SharedMemoryEngine()
    : _context{-1, -1, -1, -1},
      _role(ROLE_COMPONENT),
      _local_port(0),
      _local_index(-1),
      _layout(nullptr),
      _nonblocking(false),
      _next_gateway_queue(0) {}

void SharedMemoryEngine::engine_init(const char * interface_name) {
    (void) interface_name;
    std::fprintf(stderr,
                 "[SharedMemoryEngine] configuracao ausente; use NIC<SharedMemoryEngine>(config)\n");
}

void SharedMemoryEngine::engine_init(const Config & config) {
    _context = config.context;
    _role = config.role;
    _local_port = config.local_port;
    _nonblocking = false;
    _next_gateway_queue = 0;

    void * raw = shmat(_context.shmid, nullptr, 0);
    if (raw == reinterpret_cast<void *>(-1)) {
        std::perror("[SharedMemoryEngine] shmat");
        _layout = nullptr;
        return;
    }

    _layout = reinterpret_cast<Layout *>(raw);
    if (_layout->magic != MAGIC) {
        std::fprintf(stderr, "[SharedMemoryEngine] layout invalido\n");
        shmdt(_layout);
        _layout = nullptr;
        return;
    }

    if (_role == ROLE_COMPONENT) {
        _local_index = index_for_port(_local_port);
        if (_local_index < 0) {
            std::fprintf(stderr, "[SharedMemoryEngine] porta local invalida: %u\n",
                         static_cast<unsigned>(_local_port));
        }

        if (_context.notify_read_fd >= 0) {
            close(_context.notify_read_fd);
            _context.notify_read_fd = -1;
        }
    } else {
        _local_index = -1;
        if (_context.notify_write_fd >= 0) {
            close(_context.notify_write_fd);
            _context.notify_write_fd = -1;
        }
    }
}

int SharedMemoryEngine::engine_send(const void * frame, unsigned int size) {
    if (!_layout || !frame || size == 0 || size > sizeof(Ethernet::Frame)) {
        return -1;
    }

    if (_role == ROLE_GATEWAY) {
        return send_from_gateway(*reinterpret_cast<const Ethernet::Frame *>(frame), size);
    }

    return send_from_component(frame, size);
}

int SharedMemoryEngine::engine_receive(void * frame, unsigned int size) {
    if (!_layout || !frame || size < Ethernet::HEADER_SIZE) {
        return -1;
    }

    if (_role == ROLE_GATEWAY) {
        return receive_for_gateway(frame, size);
    }

    return receive_for_component(frame, size);
}

void SharedMemoryEngine::engine_close() {
    if (_layout) {
        shmdt(_layout);
        _layout = nullptr;
    }

    if (_context.notify_read_fd >= 0) {
        close(_context.notify_read_fd);
        _context.notify_read_fd = -1;
    }

    if (_context.notify_write_fd >= 0) {
        close(_context.notify_write_fd);
        _context.notify_write_fd = -1;
    }
}

void SharedMemoryEngine::engine_get_address(unsigned char * mac) {
    if (!mac) {
        return;
    }

    std::memset(mac, 0, Ethernet::Address::LENGTH);
    if (!_layout) {
        return;
    }

    Ethernet::Address address = (_role == ROLE_GATEWAY)
        ? _layout->gateway_address
        : _layout->component_addresses[_local_index];
    std::memcpy(mac, address.raw(), Ethernet::Address::LENGTH);
}

int SharedMemoryEngine::engine_fd() const {
    if (_role != ROLE_GATEWAY) {
        return -1;
    }
    return _context.notify_read_fd;
}

void SharedMemoryEngine::engine_set_nonblocking(bool enabled) {
    _nonblocking = enabled;

    if (_role == ROLE_GATEWAY && _context.notify_read_fd >= 0) {
        int flags = fcntl(_context.notify_read_fd, F_GETFL, 0);
        if (flags < 0) {
            std::perror("[SharedMemoryEngine] fcntl(F_GETFL)");
            return;
        }

        if (enabled) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }

        if (fcntl(_context.notify_read_fd, F_SETFL, flags) < 0) {
            std::perror("[SharedMemoryEngine] fcntl(F_SETFL)");
        }
    }
}

int SharedMemoryEngine::sem_to_gateway_empty(unsigned int index) {
    return static_cast<int>(index * 4);
}

int SharedMemoryEngine::sem_to_gateway_full(unsigned int index) {
    return static_cast<int>(index * 4 + 1);
}

int SharedMemoryEngine::sem_to_component_empty(unsigned int index) {
    return static_cast<int>(index * 4 + 2);
}

int SharedMemoryEngine::sem_to_component_full(unsigned int index) {
    return static_cast<int>(index * 4 + 3);
}

int SharedMemoryEngine::index_for_port(uint16_t port) const {
    if (!_layout) {
        return -1;
    }

    for (unsigned int i = 0; i < _layout->component_count; ++i) {
        if (_layout->component_ports[i] == port) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int SharedMemoryEngine::index_for_component_address(const Ethernet::Address & address) const {
    if (!_layout) {
        return -1;
    }

    for (unsigned int i = 0; i < _layout->component_count; ++i) {
        if (_layout->component_addresses[i] == address) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool SharedMemoryEngine::queue_send(Queue * queue,
                                    int empty_sem,
                                    int full_sem,
                                    const void * frame,
                                    unsigned int size) {
    if (!queue || !frame) {
        return false;
    }

    bool reserved = _nonblocking ? try_take_semaphore(empty_sem) : wait_semaphore(empty_sem);
    if (!reserved) {
        errno = EAGAIN;
        return false;
    }

    Slot & slot = queue->slots[queue->tail];
    slot.size = size;
    std::memcpy(slot.frame, frame, size);
    queue->tail = (queue->tail + 1) % QUEUE_LENGTH;

    if (!signal_semaphore(full_sem)) {
        return false;
    }

    return true;
}

int SharedMemoryEngine::queue_receive(Queue * queue,
                                      int empty_sem,
                                      int full_sem,
                                      void * frame,
                                      unsigned int size) {
    if (!queue || !frame) {
        return -1;
    }

    bool available = _nonblocking ? try_take_semaphore(full_sem) : wait_semaphore(full_sem);
    if (!available) {
        errno = EAGAIN;
        return -1;
    }

    Slot & slot = queue->slots[queue->head];
    unsigned int copy_size = slot.size;
    if (copy_size > size) {
        copy_size = size;
    }

    std::memcpy(frame, slot.frame, copy_size);
    queue->head = (queue->head + 1) % QUEUE_LENGTH;
    signal_semaphore(empty_sem);
    return static_cast<int>(copy_size);
}

bool SharedMemoryEngine::wait_semaphore(int index) {
    sembuf op = {};
    op.sem_num = static_cast<unsigned short>(index);
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

bool SharedMemoryEngine::signal_semaphore(int index) {
    sembuf op = {};
    op.sem_num = static_cast<unsigned short>(index);
    op.sem_op = 1;
    op.sem_flg = 0;

    while (semop(_context.semid, &op, 1) < 0) {
        if (errno == EINTR) {
            continue;
        }
        std::perror("[SharedMemoryEngine] semop(signal)");
        return false;
    }

    return true;
}

bool SharedMemoryEngine::try_take_semaphore(int index) {
    sembuf op = {};
    op.sem_num = static_cast<unsigned short>(index);
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT;

    while (semop(_context.semid, &op, 1) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN) {
            return false;
        }
        std::perror("[SharedMemoryEngine] semop(try)");
        return false;
    }

    return true;
}

void SharedMemoryEngine::notify_gateway() {
    if (_context.notify_write_fd < 0) {
        return;
    }

    const unsigned char token = 0x01;
    ssize_t written = write(_context.notify_write_fd, &token, sizeof(token));
    if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        std::perror("[SharedMemoryEngine] write(notify)");
    }
}

void SharedMemoryEngine::drain_gateway_notification() {
    if (_context.notify_read_fd < 0) {
        return;
    }

    unsigned char token = 0;
    ssize_t rc = read(_context.notify_read_fd, &token, sizeof(token));
    (void) rc;
}

int SharedMemoryEngine::receive_for_gateway(void * frame, unsigned int size) {
    if (_nonblocking) {
        for (unsigned int offset = 0; offset < _layout->component_count; ++offset) {
            unsigned int index = (_next_gateway_queue + offset) % _layout->component_count;
            Queue * queue = &_layout->to_gateway[index];
            int rc = queue_receive(queue,
                                   sem_to_gateway_empty(index),
                                   sem_to_gateway_full(index),
                                   frame,
                                   size);
            if (rc > 0) {
                _next_gateway_queue = (index + 1) % _layout->component_count;
                drain_gateway_notification();
                return rc;
            }
        }

        errno = EAGAIN;
        drain_gateway_notification();
        return -1;
    }

    while (true) {
        if (_context.notify_read_fd < 0) {
            errno = EINVAL;
            return -1;
        }

        unsigned char token = 0;
        while (read(_context.notify_read_fd, &token, sizeof(token)) < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -1;
        }

        for (unsigned int offset = 0; offset < _layout->component_count; ++offset) {
            unsigned int index = (_next_gateway_queue + offset) % _layout->component_count;
            if (!try_take_semaphore(sem_to_gateway_full(index))) {
                continue;
            }

            Queue & queue = _layout->to_gateway[index];
            Slot & slot = queue.slots[queue.head];
            unsigned int copy_size = (slot.size > size) ? size : slot.size;
            std::memcpy(frame, slot.frame, copy_size);
            queue.head = (queue.head + 1) % QUEUE_LENGTH;
            signal_semaphore(sem_to_gateway_empty(index));
            _next_gateway_queue = (index + 1) % _layout->component_count;
            return static_cast<int>(copy_size);
        }
    }
}

int SharedMemoryEngine::receive_for_component(void * frame, unsigned int size) {
    if (_local_index < 0) {
        errno = EINVAL;
        return -1;
    }

    return queue_receive(&_layout->to_component[_local_index],
                         sem_to_component_empty(_local_index),
                         sem_to_component_full(_local_index),
                         frame,
                         size);
}

int SharedMemoryEngine::send_from_gateway(const Ethernet::Frame & frame, unsigned int size) {
    bool delivered = false;
    int result = -1;

    if (frame.dst() == Ethernet::Address::BROADCAST) {
        for (unsigned int i = 0; i < _layout->component_count; ++i) {
            if (queue_send(&_layout->to_component[i],
                           sem_to_component_empty(i),
                           sem_to_component_full(i),
                           &frame,
                           size)) {
                delivered = true;
                result = static_cast<int>(size);
            }
        }
        if (!delivered) {
            errno = EAGAIN;
        }
        return result;
    }

    int index = index_for_component_address(frame.dst());
    if (index < 0) {
        errno = ENOENT;
        return -1;
    }

    if (!queue_send(&_layout->to_component[index],
                    sem_to_component_empty(index),
                    sem_to_component_full(index),
                    &frame,
                    size)) {
        errno = EAGAIN;
        return -1;
    }

    return static_cast<int>(size);
}

int SharedMemoryEngine::send_from_component(const void * frame, unsigned int size) {
    if (_local_index < 0) {
        errno = EINVAL;
        return -1;
    }

    if (!queue_send(&_layout->to_gateway[_local_index],
                    sem_to_gateway_empty(_local_index),
                    sem_to_gateway_full(_local_index),
                    frame,
                    size)) {
        errno = EAGAIN;
        return -1;
    }

    notify_gateway();
    return static_cast<int>(size);
}

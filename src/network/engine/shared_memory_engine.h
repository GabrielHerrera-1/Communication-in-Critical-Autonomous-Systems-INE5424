#ifndef SHARED_MEMORY_ENGINE_H
#define SHARED_MEMORY_ENGINE_H

#include "../ethernet.h"
#include <sys/types.h>
#include <cstdint>
#include <vector>

class SharedMemoryEngine {
public:
    static const unsigned int MAX_COMPONENTS = 32;
    static const unsigned int QUEUE_LENGTH = 8;

    struct Context {
        int shmid;
        int semid;
        int notify_read_fd;
        int notify_write_fd;
    };

    enum Role {
        ROLE_GATEWAY,
        ROLE_COMPONENT
    };

    struct Config {
        Context context;
        Role role;
        uint16_t local_port;
    };

protected:
    struct Slot {
        unsigned int size;
        unsigned char frame[sizeof(Ethernet::Frame)];
    };

    struct Queue {
        unsigned int head;
        unsigned int tail;
        Slot slots[QUEUE_LENGTH];
    };

    struct Layout {
        unsigned int magic;
        unsigned int component_count;
        uint16_t component_ports[MAX_COMPONENTS];
        Ethernet::Address component_addresses[MAX_COMPONENTS];
        Ethernet::Address gateway_address;
        Queue to_gateway[MAX_COMPONENTS];
        Queue to_component[MAX_COMPONENTS];
    };

public:
    static Context create(const std::vector<uint16_t> & component_ports);
    static void destroy(const Context & context);

    static Ethernet::Address gateway_address();
    static Ethernet::Address component_address(uint16_t port);

protected:
    SharedMemoryEngine();
    void engine_init(const char * interface_name);
    void engine_init(const Config & config);
    int engine_send(const void * frame, unsigned int size);
    int engine_receive(void * frame, unsigned int size);
    void engine_close();
    void engine_get_address(unsigned char * mac);
    int engine_fd() const;
    void engine_set_nonblocking(bool enabled);

private:
    static const unsigned int MAGIC = 0x534f3249; // "SO2I"

    static int sem_to_gateway_empty(unsigned int index);
    static int sem_to_gateway_full(unsigned int index);
    static int sem_to_component_empty(unsigned int index);
    static int sem_to_component_full(unsigned int index);

    int index_for_port(uint16_t port) const;
    int index_for_component_address(const Ethernet::Address & address) const;
    bool queue_send(Queue * queue, int empty_sem, int full_sem, const void * frame, unsigned int size);
    int queue_receive(Queue * queue, int empty_sem, int full_sem, void * frame, unsigned int size);

    bool wait_semaphore(int index);
    bool signal_semaphore(int index);
    bool try_take_semaphore(int index);
    void notify_gateway();
    void drain_gateway_notification();
    int receive_for_gateway(void * frame, unsigned int size);
    int receive_for_component(void * frame, unsigned int size);
    int send_from_gateway(const Ethernet::Frame & frame, unsigned int size);
    int send_from_component(const void * frame, unsigned int size);

private:
    Context _context;
    Role _role;
    uint16_t _local_port;
    int _local_index;
    Layout * _layout;
    bool _nonblocking;
    unsigned int _next_gateway_queue;
};

#endif

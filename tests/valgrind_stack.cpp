#include "../src/network/nic.h"
#include "../src/channel/protocol.h"
#include "../src/communication/channel_endpoint.h"
#include "../src/communication/message.h"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

class Host_Loopback_Engine {
public:
    Host_Loopback_Engine()
        : _local_mac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
          _remote_mac{0x02, 0x00, 0x00, 0x00, 0x00, 0xA1},
          _closed(false) {}

    void engine_init(const char *) {}

    void engine_close() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _closed = true;
        }
        _cv.notify_all();
    }

    int engine_send(const void *frame, unsigned int size) {
        std::vector<unsigned char> packet(
            static_cast<const unsigned char *>(frame),
            static_cast<const unsigned char *>(frame) + size
        );

        // Synthesize a remote sender so NIC::recv_loop() does not treat the
        // frame as self-loopback and discard it.
        if (packet.size() >= Ethernet::HEADER_SIZE) {
            std::memcpy(packet.data() + Ethernet::Address::LENGTH, _remote_mac, Ethernet::Address::LENGTH);
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _queue.push_back(packet);
        }
        _cv.notify_one();
        return static_cast<int>(size);
    }

    int engine_receive(void *frame, unsigned int size) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [this] { return _closed || !_queue.empty(); });

        if (_queue.empty()) {
            return 0;
        }

        std::vector<unsigned char> packet = _queue.front();
        _queue.pop_front();
        lock.unlock();

        unsigned int copy_size = packet.size();
        if (copy_size > size) {
            copy_size = size;
        }
        std::memcpy(frame, packet.data(), copy_size);
        return static_cast<int>(copy_size);
    }

    void engine_get_address(unsigned char *mac) {
        std::memcpy(mac, _local_mac, Ethernet::Address::LENGTH);
    }

private:
    unsigned char _local_mac[Ethernet::Address::LENGTH];
    unsigned char _remote_mac[Ethernet::Address::LENGTH];
    bool _closed;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::deque<std::vector<unsigned char>> _queue;
};

template <>
struct Traits<NIC<Host_Loopback_Engine>> {
    static const unsigned int SEND_BUFFERS = 8;
    static const unsigned int RECEIVE_BUFFERS = 8;
    static constexpr const char *INTERFACE = "host-loopback";
};

template <>
struct Traits<Protocol<NIC<Host_Loopback_Engine>>> {
    static const unsigned short ETHERNET_PROTOCOL_NUMBER = 0x88E1;
};

class Host_Test_Protocol : public Protocol<NIC<Host_Loopback_Engine>> {
public:
    explicit Host_Test_Protocol(NIC<Host_Loopback_Engine> *nic)
        : Protocol<NIC<Host_Loopback_Engine>>(nic) {}
};

int main() {
    typedef NIC<Host_Loopback_Engine> Test_NIC;
    static const int ITERATIONS = 1000;

    Test_NIC nic;
    Host_Test_Protocol protocol(&nic);
    Channel_Endpoint<Host_Test_Protocol> endpoint(&protocol, protocol.create_address(0x1234));

    for (int i = 1; i <= ITERATIONS; ++i) {
        char payload[64];
        std::snprintf(payload, sizeof(payload), "valgrind-stack-smoke:%d", i);

        Message outbound(payload, std::strlen(payload) + 1);
        if (!endpoint.send(&outbound)) {
            return 1;
        }

        Message inbound;
        if (!endpoint.receive(&inbound)) {
            return 1;
        }

        const char *received = reinterpret_cast<const char *>(inbound.data());
        if (std::strcmp(payload, received) != 0) {
            return 1;
        }
    }

    return 0;
}

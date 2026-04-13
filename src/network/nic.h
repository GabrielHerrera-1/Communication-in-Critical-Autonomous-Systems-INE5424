#ifndef NIC_H
#define NIC_H

#include "ethernet.h"
#include "../core/observers/conditional_data_observer.h"
#include "../core/buffer.h"
#include "../core/traits.h"
#include <mutex>
#include <cstring>
#include <cstdio>
#include <stack>

// Network
template <typename Engine>
// NIC herda de ethernet, conditionally data observed e engine
// o dispatch agora é explícito: quem estiver no controle do fluxo (gateway ou
// communicator) chama dispatch_once()/dispatch_all() em contexto normal.
class NIC : public Ethernet, public Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Ethernet::Protocol>, private Engine
{
public:
    static const unsigned int BUFFER_SIZE =
        Traits<NIC<Engine>>::SEND_BUFFERS +
        Traits<NIC<Engine>>::RECEIVE_BUFFERS;
    typedef Ethernet::Address Address;
    typedef Ethernet::Protocol Protocol_Number;
    typedef Conditional_Data_Observer<Buffer<Ethernet::Frame>, Ethernet::Protocol> Observer;
    typedef Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Ethernet::Protocol> Observed;

    static bool is_gateway_process() {
        return Engine::is_gateway_process();
    }

    struct Statistics {
        unsigned int tx_packets, rx_packets, tx_bytes, rx_bytes;
        Statistics() : tx_packets(0), rx_packets(0), tx_bytes(0), rx_bytes(0) {}
    };

    NIC() {
        Engine::engine_init(Traits<NIC<Engine>>::INTERFACE);

        unsigned char mac[6];
        Engine::engine_get_address(mac);
        _address = Address(mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);        

        for (unsigned int i = 0; i < BUFFER_SIZE; i++) {
            _free_list.push(i);
        }
    }

    ~NIC() {
        Engine::engine_close();
    };

    int send(Address dst, Protocol_Number prot, const void *data, unsigned int size) {
        Buffer<Ethernet::Frame> *buf = alloc(dst, prot, size);
        if (!buf) return -1;
        memcpy(buf->data()->payload(), data, size);
        return send(buf);
    };

    // procura buffer livre no pool, trava ele e monta header ethernet
    Buffer<Ethernet::Frame> *alloc(Address dst, Protocol_Number prot, unsigned int size) {
        if (size > Ethernet::MTU) return nullptr;
        Buffer<Ethernet::Frame> *buf = alloc_buf();
        if (!buf) return nullptr;
        buf->size(size);
        Ethernet::Frame *f = buf->data();
        memcpy(f->dst().raw(), dst.raw(), 6);
        memcpy(f->src().raw(), _address.raw(), 6);
        f->type(prot);
        return buf;
    };

    // envia o frame montado pelo engine e libera o buffer
    int send(Buffer<Ethernet::Frame> *buf) {
        int bytes = Engine::engine_send(buf->data(), Ethernet::HEADER_SIZE + buf->size());
        if (bytes > 0) {
            _statistics.tx_packets++;
            _statistics.tx_bytes += bytes;
        }
        free(buf);
        return bytes;
    };

    void free(Buffer<Ethernet::Frame> *buf) {
        if (!buf) return;
        std::lock_guard<std::mutex> lock(_buf_mtx);
        unsigned int idx = static_cast<unsigned int>(buf - _buffer);
        _free_list.push(idx);
    };

    int unmarshal(Buffer<Ethernet::Frame> *buf, Address *src, Address *dst, void *data, unsigned int size) {
        Ethernet::Frame *f = buf->data();
        if (src) *src = f->src();
        if (dst) *dst = f->dst();
        unsigned int copy = buf->size();
        if (copy > size) copy = size;
        if (data && copy) memcpy(data, f->payload(), copy);
        return copy;
    };

    const Address &address() { return _address; };
    void address(Address address) { _address = address; };
    const Statistics &statistics() { return _statistics; };

    bool owns(const Buffer<Ethernet::Frame> *buf) const {
        return (buf >= &_buffer[0] && buf < &_buffer[BUFFER_SIZE]);
    };

    void attach(Observer *obs, Protocol_Number prot) {
        Observed::attach(obs, prot);
    };

    void detach(Observer *obs, Protocol_Number prot) {
        Observed::detach(obs, prot);
    };

    void set_nonblocking(bool enabled) {
        Engine::engine_set_nonblocking(enabled);
    }

    int wait_descriptor() const {
        return Engine::engine_wait_descriptor();
    }

    bool dispatch_once(bool block) {
        Buffer<Ethernet::Frame> *buf = alloc_buf();
        if (!buf) {
            return false;
        }

        Engine::engine_set_nonblocking(!block);
        int bytes = Engine::engine_receive(buf->data(), sizeof(Ethernet::Frame));
        if (bytes <= 0) {
            free(buf);
            return false;
        }

        if (static_cast<unsigned int>(bytes) > sizeof(Ethernet::Frame)) {
            bytes = sizeof(Ethernet::Frame);
        }

        Ethernet::Frame *frame = buf->data();
        if (static_cast<unsigned int>(bytes) < Ethernet::HEADER_SIZE) {
            free(buf);
            return false;
        }

        if (Engine::engine_should_drop_frame(*frame, _address)) {
            free(buf);
            return false;
        }

        Protocol_Number prot = frame->type();
        _statistics.rx_packets++;
        _statistics.rx_bytes += bytes;
        buf->size(static_cast<unsigned int>(bytes) - Ethernet::HEADER_SIZE);

        if (!Observed::notify(prot, buf)) {
            free(buf);
        }

        return true;
    }

    bool dispatch_all(bool block_first) {
        bool handled = false;

        if (block_first) {
            handled = dispatch_once(true);
        }

        while (dispatch_once(false)) {
            handled = true;
        }

        return handled;
    }

private:
    Buffer<Ethernet::Frame> *alloc_buf() {
        std::lock_guard<std::mutex> lock(_buf_mtx);
        if (_free_list.empty()) return nullptr;
        unsigned int idx = _free_list.top();
        _free_list.pop();
        return &_buffer[idx];
    }

    Address _address;
    std::mutex _buf_mtx;
    Statistics _statistics;
    Buffer<Ethernet::Frame> _buffer[BUFFER_SIZE];
    std::stack<unsigned int> _free_list;
};

#endif

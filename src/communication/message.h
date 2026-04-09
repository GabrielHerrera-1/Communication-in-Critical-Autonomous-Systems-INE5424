#ifndef MESSAGE_H
#define MESSAGE_H

#include "../network/ethernet.h"
#include <cstring>
#include <cstdint>

class Message {
public:
    struct Origin {
        Origin() : address(), port(0) {}

        Origin(const Ethernet::Address & source_address, uint16_t source_port)
            : address(source_address), port(source_port) {}

        Ethernet::Address address;
        uint16_t port;
    };

    static const unsigned int MAX_SIZE = 1400;

    Message() : _size(MAX_SIZE), _origin(), _has_origin(false) {
        memset(_payload, 0, sizeof(_payload));
    }

    Message(const void* data, unsigned int size) : _size(0), _origin(), _has_origin(false) {
        memset(_payload, 0, sizeof(_payload));
        if (data && size > 0) {
            _size = (size <= MAX_SIZE) ? size : MAX_SIZE;
            memcpy(_payload, data, _size);
        }
    }

    //Métodos exigidos pelo Communicator

    const void* data() const {
        return _payload;
    }

    void* data() {
        return _payload;
    }

    unsigned int size() const {
        return _size;
    }

    void size(unsigned int s) {
        _size = (s <= MAX_SIZE) ? s : MAX_SIZE;
    }

    bool has_origin() const {
        return _has_origin;
    }

    const Origin & origin() const {
        return _origin;
    }

    void origin(const Origin & source_origin) {
        _origin = source_origin;
        _has_origin = true;
    }

    bool has_source_port() const {
        return has_origin();
    }

    uint16_t source_port() const {
        return _origin.port;
    }

    Ethernet::Address source_address() const {
        return _origin.address;
    }

    void source_port(uint16_t port) {
        _origin.port = port;
        _has_origin = true;
    }

    void source_address(const Ethernet::Address & address) {
        _origin.address = address;
        _has_origin = true;
    }

    void clear_origin() {
        _origin = Origin();
        _has_origin = false;
    }

    void clear_source_port() {
        clear_origin();
    }

private:
    unsigned char _payload[MAX_SIZE];
    unsigned int _size;
    Origin _origin;
    bool _has_origin;
};

#endif

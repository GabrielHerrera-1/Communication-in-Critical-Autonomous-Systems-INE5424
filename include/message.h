#ifndef MESSAGE_H
#define MESSAGE_H

#include <cstring>

class Message {
public:

    static const unsigned int MAX_SIZE = 1400;

    Message() : _size(0) {
        memset(_payload, 0, sizeof(_payload));
    }

    Message(const void* data, unsigned int size) : _size(0) {
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

private:
    unsigned char _payload[MAX_SIZE];
    unsigned int _size;
};

#endif

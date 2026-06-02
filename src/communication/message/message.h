#ifndef MESSAGE_H
#define MESSAGE_H
	
#include <cstring>
#include <cstdint>
#include "../../channel/vehicle_protocol.h"
#include "./message_header.h"

class Message : public MessageHeader{
public:
    
    template <typename Channel>
    friend class Communicator;

    Message(): MessageHeader(){
        memset(_payload, 0, sizeof(_payload));
    }

    Message(const void* data, unsigned int size): MessageHeader(size){
        memset(_payload, 0, sizeof(_payload));
        if (data && size > 0) {
            _size = (size <= MAX_SIZE) ? size : MAX_SIZE;
            memcpy(_payload, data, _size);
        }
    }

    const void* data() const {
        return _payload;
    }

    void* data() {
        return _payload;
    }

private:
    unsigned char _payload[MAX_SIZE];
};

#endif
#ifndef MESSAGE_H
#define MESSAGE_H
	
#include <cstring>
#include <cstdint>
#include "../../channel/vehicle_protocol.h"
#include "./message_header.h"

struct RawPayload {
    unsigned char data[MessageHeader::MAX_SIZE];
};

template <typename PayloadType = RawPayload>
class TypedMessage : public MessageHeader{
public:
    
    template <typename Channel>
    friend class Communicator;

    TypedMessage(): MessageHeader(){
        memset(&_payload, 0, sizeof(_payload));
    }

    TypedMessage(const void* data, unsigned int size): MessageHeader(size){
        memset(&_payload, 0, sizeof(_payload));
        if (data && size > 0) {
            _size = (size <= MAX_SIZE) ? size : MAX_SIZE;
            memcpy(&_payload, data, _size);
        }   
    }

    TypedMessage(const PayloadType& customPayload) : MessageHeader(sizeof(PayloadType)) {
        _payload = customPayload;
    }

    const void* data() const { return &_payload; }

    void* data() { return &_payload; }

    const PayloadType& payload() const { return _payload; }

    PayloadType& payload() { return _payload; }

private:
    PayloadType _payload;
};

using Message = TypedMessage<RawPayload>;

#endif
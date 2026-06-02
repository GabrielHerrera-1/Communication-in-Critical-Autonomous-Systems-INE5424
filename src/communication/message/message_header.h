#ifndef MESSAGE_HEADER_H
#define MESSAGE_HEADER_H

#include "../../channel/vehicle_protocol.h"

class MessageHeader {
public:

    MessageHeader():
        _size(MAX_SIZE),
        _type(STANDARD)
    {}

    MessageHeader(unsigned int size):
        _size(size),
        _type(STANDARD)
    {}

    ~MessageHeader(){}

    enum Type : uint8_t {
        STANDARD = 0x00,
        INTEREST = 0x01
    };

    class Origin{
    public:
        Origin(){}

        ~Origin(){}

        const Vehicle_Protocol::Port port() const { return address.port(); } 

        const Vehicle_Protocol::Physical_Address physical_address() const { return address.paddr(); }

        Vehicle_Protocol::Address address;
        uint8_t quadrant;   

    };

    static const unsigned int MAX_SIZE = 1400;
    static const uint8_t QUADRANT_NONE = 0xFF;

    unsigned int size(){
        return _size;
    }

    const Origin & origin(){
        return _origin;
    }

    Vehicle_Protocol::Address address(){
        return _origin.address;
    }

    uint8_t quadrant(){
        return _origin.quadrant;
    }

    int64_t timestamp(){
        return _timestamp;
    }
    
    uint8_t type(){
        return _type;
    }

    void type(uint8_t type){
        _type = type;
    }

    void size(unsigned int size){
        _size = size;
    }

protected:

    void origin(Origin & origin){
        _origin = origin;
    }

    void address(Vehicle_Protocol::Address addr){
        _origin.address = addr;
    }

    void quadrant(uint8_t q){
        _origin.quadrant = q;
    }

    void timestamp(int64_t timestamp){
        _timestamp = timestamp;
    }

    Origin _origin;
    int64_t _timestamp;
    uint8_t _type = STANDARD;
    unsigned int _size;

};

#endif
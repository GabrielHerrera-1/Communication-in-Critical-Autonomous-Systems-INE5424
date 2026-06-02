#ifndef MESSAGE_HEADER_H
#define MESSAGE_HEADER_H

#include "../../channel/vehicle_protocol.h"
#include "../communicator.h"

class MessageHeader {
    friend class Communicator;
public:

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

    unsigned int size();

    const Origin & origin();

    Vehicle_Protocol::Address address();

    uint8_t quadrant() const;

    int64_t timestamp() const;
    
    uint8_t type() const;

protected:

    void size(unsigned int);

    void origin(Origin & origin);

    void address(Vehicle_Protocol::Address addr);

    void quadrant(uint8_t quadrant);

    void timestamp(int64_t timestamp);
    
    void type(uint8_t type);

private:

    Origin origin;
    int64_t timestamp = 0;
    uint8_t type = STANDARD;
    unsigned int _size;

};

#endif
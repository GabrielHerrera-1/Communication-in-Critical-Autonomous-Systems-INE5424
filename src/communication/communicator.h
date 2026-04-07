#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message.h"

template <typename Channel>
class Communicator {
public:
    typedef typename Channel::Address Address;

public:
    Communicator(Channel * channel, Address address): _channel(channel), _address(address) {}

    ~Communicator() = default;

    bool send(const Message * message) {
        return (_channel->send(_address, Channel::Address::broadcast(_address.port()),
                               message->data(), message->size()) > 0);
    }

    bool receive(Message * message) {
        typename Channel::Address from;
        int size = _channel->receive(_address, &from, message->data(), message->size());
        message->size(size);

        if (size <= 0)
            return false;
        
        return true;
    }

private:
    Channel * _channel;
    Address _address;
};

#endif

#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message.h"
#include "../core/observers/concurrent_observer.h"

template <typename Channel>
class Communicator: public Concurrent_Observer<typename Channel::Observer::Observed_Data, typename Channel::Observer::Observing_Condition> {

    typedef Concurrent_Observer<typename Channel::Observer::Observed_Data, typename Channel::Observer::Observing_Condition> Observer;

public:
    typedef typename Channel::Buffer Buffer;
    typedef typename Channel::Address Address;

public:
    Communicator(Channel * channel, Address address): Observer(), _channel(channel), _address(address) {
        _channel->attach(this, address);
    }

    ~Communicator() { _channel->detach(this, _address); }

    bool send(const Message * message) {
        return (_channel->send(_address, Channel::Address::broadcast(_address.port()),
                               message->data(), message->size()) > 0);
    }

    bool receive(Message * message) {
        Buffer * buf = nullptr;
        while (!Observer::try_updated(&buf)) {
            int dispatched = _channel->dispatch_once();
            if (dispatched < 0) {
                continue;
            }
        }

        if (!buf) {
            //print("ERROR: No aviable buffer");
            return false;
        }

        typename Channel::Address from;
        int size = _channel->receive(buf, &from, message->data(), message->size());
        message->size(size);
        message->origin(Message::Origin(from.paddr(), from.port()));

        if (size <= 0)
            return false;
        
        return true;
    }

private:
    void update(typename Channel::Observer::Observing_Condition c, Buffer * buf) {
        Observer::update(c, buf);
    }

private:
    Channel * _channel;
    Address _address;
};

#endif

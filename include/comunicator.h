#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message.h"
#include "observers/concurrent_observer.h"

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
        // cout << "Mensagem enviada: " << message->const_data() << endl;
        return (_channel->send(_address, Channel::Address::BROADCAST, message->data(), message->size()) > 0);
    }

    bool receive(Message * message) {
        Buffer * buf = Observer::updated(); // block until a notification is triggered

        if (!buf) {
            //print("ERROR: No aviable buffer");
            return false;
        }

        typename Channel::Address from;
        int size = _channel->receive(buf, &from, message->data(), message->size());
        message->size(size);

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
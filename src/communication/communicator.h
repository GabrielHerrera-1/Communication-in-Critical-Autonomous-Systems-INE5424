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
        // O componente observa a propria porta e o grupo de broadcast logico.
        // Isso preserva portas individuais para identificar origem/resposta,
        // sem depender de um valor "magico" vindo da camada de aplicacao.
        _broadcast_address = Address::logical_broadcast();
        _channel->attach(this, _broadcast_address);
    }

    ~Communicator() {
        _channel->detach(this, _address);
        _channel->detach(this, _broadcast_address);
    }

    bool send(const Message * message) {
        return (_channel->send(_address, Address::logical_broadcast(),
                               message->data(), message->size()) > 0);
    }

    bool try_receive(Message * message) {
        Buffer * buf = nullptr;
        if (!Observer::try_updated(&buf)) {
            _channel->dispatch(false);
            if (!Observer::try_updated(&buf)) {
                return false;
            }
        }

        return consume_buffer(buf, message);
    }

    bool receive(Message * message) {
        Buffer * buf = nullptr;
        while (!Observer::try_updated(&buf)) {
            _channel->dispatch(true);
        }

        return consume_buffer(buf, message);
    }

private:
    void update(typename Channel::Observer::Observing_Condition c, Buffer * buf) {
        Observer::update(c, buf);
    }

    bool consume_buffer(Buffer * buf, Message * message) {
        if (!buf || !message) {
            return false;
        }

        typename Channel::Address from;
        int size = _channel->receive(buf, &from, message->data(), message->size());
        message->size(size);
        message->origin(typename Message::Origin(from.paddr(), from.port()));

        return size > 0;
    }

private:
    Channel * _channel;
    Address _address;
    Address _broadcast_address;
};

#endif

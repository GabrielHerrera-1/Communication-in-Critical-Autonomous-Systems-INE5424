#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message/message.h"
#include "../core/clock.h"
#include "../core/observers/concurrent_observer.h"

template <typename Channel>
class Communicator: public Concurrent_Observer<typename Channel::Observer::Observed_Data, typename Channel::Observer::Observing_Condition> {

    typedef Concurrent_Observer<typename Channel::Observer::Observed_Data, typename Channel::Observer::Observing_Condition> Observer;

public:
    typedef typename Channel::Buffer Buffer;
    typedef typename Channel::Address Address;

public:
    Communicator(Channel * channel, Address address, bool subscribe_broadcast = true)
        : Observer(),
          _channel(channel),
          _address(address),
          _broadcast_address(Address::logical_broadcast()),
          _subscribed_to_broadcast(subscribe_broadcast) {
        _channel->attach(this, address);
        // Alguns componentes sao send-only e nunca drenam a fila entregue ao
        // broadcast logico. Nesses casos mantemos apenas a escuta da propria
        // porta para evitar exaurir o pool de buffers local.
        if (_subscribed_to_broadcast) {
            _channel->attach(this, _broadcast_address);
        }
    }

    ~Communicator() {
        _channel->detach(this, _address);
        if (_subscribed_to_broadcast) {
            _channel->detach(this, _broadcast_address);
        }
    }

    // TODO: tirei o const pra colocar o timestamp aqui, ver se é necessario msm
    bool send(Message * message) {
        message->timestamp(Clock::monotonic_stamp());
        return (_channel->send(_address, Address::logical_broadcast(),
                               message->data(), message->size(),
                               message->timestamp()) > 0);
    }

    bool receive(Message * message) {
        Buffer * buf = Observer::updated(); // block until a notification is triggered

        if (!buf) {
            //print("ERROR: No aviable buffer");
            return false;
        }

        typename Channel::Address from;
        int64_t ts = 0;
        uint8_t q = Message::QUADRANT_NONE; // default 
        int size = _channel->receive(buf, &from, &ts, &q, message->data(), message->size());
        message->size(size);
        message->address(from);
        message->timestamp(ts);
        message->quadrant(q);

        if (size <= 0)
            return false;

        return true;
    }


protected:

    Communicator(Channel * channel, Channel::Port port)
        : Observer(),
          _channel(channel),
          _broadcast_address(Address::logical_broadcast()),
          _subscribed_to_broadcast(subscribe_broadcast) {
        _address = _channel->create_address(port)
    }

    void update(typename Channel::Observer::Observing_Condition c, Buffer * buf) {
        Observer::update(c, buf);
    }

    Channel * _channel;
    Address _address;
    Address _broadcast_address;
    bool _subscribed_to_broadcast;
};

#endif

#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message.h"
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
        message->_timestamp = Clock::monotonic_stamp();
        return (_channel->send(_address, Address::logical_broadcast(),
                               message->data(), message->size(),
                               message->_timestamp) > 0);
    }

    bool receive(Message * message) {
        Buffer * buf = Observer::updated(); // block until a notification is triggered

        if (!buf) {
            //print("ERROR: No aviable buffer");
            return false;
        }

        typename Channel::Address from;
        int64_t ts = 0;
        int size = _channel->receive(buf, &from, &ts, message->data(), message->size());
        message->_size = size;
        message->_origin = from;
        message->_timestamp = ts;

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
    Address _broadcast_address;
    bool _subscribed_to_broadcast;
};

#endif

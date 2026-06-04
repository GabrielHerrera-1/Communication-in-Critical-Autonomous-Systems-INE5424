#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message/message.h"
#include "../core/clock.h"
#include "../core/observers/concurrent_observer.h"

// O Communicator e um Concurrent_Observer: o canal (Protocol) o notifica via
// update(). O update() PADRAO (protected) empilha o buffer na fila do
// Concurrent_Observer, drenada pelo receive() bloqueante -- modelo classico,
// usado pelos testes legados. Subclasses (ex.: SmartData) SOBRESCREVEM update()
// para interpretar a mensagem na hora, SEM empilhar na fila do Concurrent_Observer.
//
// Attach/detach do canal sao explicitos: o ctor publico (Address) ja faz attach;
// o ctor protegido (Port) NAO, para que subclasses chamem attach_channel() so
// depois de inicializar seus membros (senao um update() virtual poderia chegar
// antes da subclasse existir). O destrutor da subclasse chama detach_channel()
// antes de destruir seus membros.
template <typename Channel>
class Communicator : public Channel::Observer {
public:
    typedef typename Channel::Buffer Buffer;
    typedef typename Channel::Address Address;
    typedef typename Channel::Observer Observer; // = Concurrent_Observer<Buffer, Port>
    typedef typename Channel::Observer::Observing_Condition Condition;

    Communicator(Channel * channel, Address address, bool subscribe_broadcast = true)
        : _channel(channel),
          _address(address),
          _broadcast_address(Address::logical_broadcast()),
          _subscribed_to_broadcast(subscribe_broadcast) {
        attach_channel();
    }

    virtual ~Communicator() {
        detach_channel();
    }

    template <typename Payload>
    bool send(TypedMessage<Payload> * message) {
        message->timestamp(Clock::monotonic_stamp());
        return (_channel->send(_address, Address::logical_broadcast(),
                               message->data(), message->size(),
                               message->timestamp()) > 0);
    }

    template <typename Payload>
    bool receive(TypedMessage<Payload> * message) {
        Buffer * buf = Observer::updated(); // drena a fila do Concurrent_Observer
        if (!buf) return false;

        Address from;
        int64_t ts = 0;
        uint8_t q = MessageHeader::QUADRANT_NONE;
        int size = _channel->receive(buf, &from, &ts, &q, message->data(), sizeof(Payload));
        message->size(size);
        message->address(from);
        message->timestamp(ts);
        message->quadrant(q);

        return size > 0;
    }

protected:
    // Ctor por Port (subclasses): deriva o Address via create_address e NAO faz
    // attach. A subclasse chama attach_channel() ao fim do seu construtor.
    Communicator(Channel * channel, typename Channel::Port port, bool subscribe_broadcast = true)
        : _channel(channel),
          _address(channel->create_address(port)),
          _broadcast_address(Address::logical_broadcast()),
          _subscribed_to_broadcast(subscribe_broadcast) {}

    // Notificacao do canal. Default: empilha na fila do Concurrent_Observer
    // (chamada qualificada = nao-virtual). E um override do Concurrent_Observer::
    // update (agora virtual); SmartData sobrescreve para NAO empilhar.
    void update(Condition c, Buffer * buf) override {
        Observer::update(c, buf);
    }

    void attach_channel() {
        if (_attached) return;
        _channel->attach(this, _address);
        if (_subscribed_to_broadcast) _channel->attach(this, _broadcast_address);
        _attached = true;
    }

    void detach_channel() {
        if (!_attached) return;
        _channel->detach(this, _address);
        if (_subscribed_to_broadcast) _channel->detach(this, _broadcast_address);
        _attached = false;
    }

    Channel * _channel;
    Address _address;
    Address _broadcast_address;
    bool _subscribed_to_broadcast;
    bool _attached = false;
};

#endif

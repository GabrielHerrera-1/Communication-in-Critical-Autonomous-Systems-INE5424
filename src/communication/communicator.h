#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message/message.h"
#include "../core/clock.h"
#include "../core/observers/concurrent_observer.h"

// O Communicator tem DOIS papeis no padrao observer:
//
//   1) E um Concurrent_Observer<Buffer> DO canal (Protocol): o Protocol o
//      notifica com buffers crus (update(porta, buffer)).
//   2) E um Concurrent_Observed<Message>: ele e OBSERVAVEL por mensagens. Ao
//      receber um buffer do canal, faz o unmarshal numa Message (TypedMessage
//      inteira) e a notifica aos seus observers (os SmartData).
//
// Assim o SmartData nao precisa drenar fila nem conhecer buffer: ele so observa
// o Communicator e recebe Messages prontas. Se NAO houver observer anexado para
// aquela condicao (componente legado), o update cai no caminho classico:
// empilha o buffer para o receive() bloqueante.
template <typename Channel>
class Communicator
    : public Channel::Observer,  // Concurrent_Observer<Buffer, Port> (do canal)
      public Concurrent_Observed<Message, typename Channel::Observer::Observing_Condition> {

public:
    typedef typename Channel::Buffer Buffer;
    typedef typename Channel::Address Address;
    typedef typename Channel::Observer::Observing_Condition Condition;
    typedef typename Channel::Observer ChannelObserver;       // Concurrent_Observer<Buffer>
    typedef Concurrent_Observed<Message, Condition> MessageObserved;

public:
    Communicator(Channel * channel, Address address, bool subscribe_broadcast = true)
        : ChannelObserver(),
          _channel(channel),
          _address(address),
          _broadcast_address(Address::logical_broadcast()),
          _subscribed_to_broadcast(subscribe_broadcast) {
        _channel->attach(this, _address);
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

    // condicao em que as mensagens de broadcast chegam (porta de broadcast).
    // O SmartData se anexa a esta condicao para receber Interesse/Resposta.
    Condition broadcast_condition() const { return _broadcast_address.port(); }

    // Permite componentes que entram tardiamente nascerem sem escutar broadcast
    // e assinarem o grupo apenas quando o SmartData existir. Assim o caminho
    // legado de receive() nao acumula buffers que ninguem vai drenar.
    void ensure_broadcast_subscription() {
        if (_subscribed_to_broadcast) return;
        _channel->attach(this, _broadcast_address);
        _subscribed_to_broadcast = true;
    }

    template <typename Payload>
    bool send(TypedMessage<Payload> * message) {
        message->timestamp(Clock::monotonic_stamp());
        return (_channel->send(_address, Address::logical_broadcast(),
                               message->data(), message->size(),
                               message->timestamp()) > 0);
    }

    // receive() legado: drena a fila de buffers do Concurrent_Observer. So e
    // usado por componentes SEM SmartData anexado (ver update()).
    template <typename Payload>
    bool receive(TypedMessage<Payload> * message) {
        Buffer * buf = ChannelObserver::updated();
        if (!buf) return false;

        Address from;
        int64_t ts = 0;
        uint8_t q = Message::QUADRANT_NONE;
        int size = _channel->receive(buf, &from, &ts, &q, message->data(), sizeof(Payload));
        message->size(size);
        message->address(from);
        message->timestamp(ts);
        message->quadrant(q);

        return size > 0;
    }

protected:
    // Ctor por Port: deriva o Address via create_address(port).
    Communicator(Channel * channel, typename Channel::Port port, bool subscribe_broadcast = true)
        : ChannelObserver(),
          _channel(channel),
          _address(channel->create_address(port)),
          _broadcast_address(Address::logical_broadcast()),
          _subscribed_to_broadcast(subscribe_broadcast) {
        _channel->attach(this, _address);
        if (_subscribed_to_broadcast) {
            _channel->attach(this, _broadcast_address);
        }
    }

    // Notificacao do canal. Se ha SmartData observando esta condicao, faz o
    // unmarshal do buffer numa Message inteira e a notifica; senao, empilha o
    // buffer para o receive() legado.
    void update(Condition c, Buffer * buf) override {
        if (MessageObserved::has_observer(c)) {
            Message * m = new Message();
            Address from;
            int64_t ts = 0;
            uint8_t q = Message::QUADRANT_NONE;
            int size = _channel->receive(buf, &from, &ts, &q, m->data(), MessageHeader::MAX_SIZE);
            m->size(size);
            m->address(from);
            m->timestamp(ts);
            m->quadrant(q);

            // a Message inteira viaja para os observers (SmartData). Quem
            // consome (enfileira) assume a posse; quem ignora, descarta.
            if (size <= 0 || !MessageObserved::notify(c, m)) {
                delete m;
            }
        } else {
            ChannelObserver::update(c, buf); // legado: empilha o buffer
        }
    }

    Channel * _channel;
    Address _address;
    Address _broadcast_address;
    bool _subscribed_to_broadcast;
};

#endif

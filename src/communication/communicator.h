#ifndef COMUNICATOR_H
#define COMUNICATOR_H

#include "message/message.h"
#include "../core/clock.h"
#include "../core/observers/conditional_data_observer.h"
#include "../core/posix_semaphore.h"
#include <queue>
#include <mutex>

// O Communicator e um Conditional_Data_Observer: o canal (Protocol) o notifica
// via update(condicao, buffer), onde a condicao e a porta. O update() PADRAO
// enfileira o buffer para o receive() bloqueante (modelo classico, usado pelos
// testes legados). Subclasses (ex.: SmartData) SOBRESCREVEM update() para ler e
// interpretar a mensagem na hora -- modelo push, sem thread/laco de drenagem.
//
// Attach/detach do canal sao explicitos (attach_channel/detach_channel). O ctor
// publico (Address) ja faz attach. Subclasses usam o ctor protegido (Port), que
// NAO faz attach, e chamam attach_channel() so DEPOIS de inicializar seus
// membros -- senao um update() virtual poderia chegar antes da subclasse
// existir. Simetricamente, a subclasse chama detach_channel() no inicio do seu
// destrutor, antes de destruir seus membros.
template <typename Channel>
class Communicator : public Channel::Observer {
public:
    typedef typename Channel::Buffer Buffer;
    typedef typename Channel::Address Address;
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
        _rx_sem.p(); // bloqueia ate update() enfileirar um buffer

        Buffer * buf = nullptr;
        {
            std::lock_guard<std::mutex> lock(_rx_mtx);
            if (_rx_queue.empty()) return false;
            buf = _rx_queue.front();
            _rx_queue.pop();
        }
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

    // Notificacao do canal (roda na thread de recepcao do Protocol). Default:
    // enfileira para o receive() bloqueante. SmartData sobrescreve para parsear.
    void update(Condition /*c*/, Buffer * buf) override {
        {
            std::lock_guard<std::mutex> lock(_rx_mtx);
            _rx_queue.push(buf);
        }
        _rx_sem.v();
    }

protected:
    // Ctor por Port (subclasses): deriva o Address via create_address e NAO faz
    // attach. A subclasse chama attach_channel() ao fim do seu construtor.
    Communicator(Channel * channel, typename Channel::Port port, bool subscribe_broadcast = true)
        : _channel(channel),
          _address(channel->create_address(port)),
          _broadcast_address(Address::logical_broadcast()),
          _subscribed_to_broadcast(subscribe_broadcast) {}

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

private:
    Semaphore _rx_sem{0};
    std::mutex _rx_mtx;
    std::queue<Buffer *> _rx_queue;
};

#endif

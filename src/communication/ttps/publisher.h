#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <vector>
#include "../communicator.h"
#include "iproducer.h"
#include "../message/message.h"

template <typename Channel, typename Data>
class Publisher: public Communicator<Channel>{
public:
    typedef typename Channel::Buffer Buffer;
    typedef typename Channel::Address Address;
    
private:

    struct Request
    {
        Address src;
        uint16_t period_ms;
    };

    // private send and receive

    // when a INTEREST comes, register it (interval), should only receive Interest
    // port c, ethernet::frame buffer 
    void update(typename Channel::Observer::Observing_Condition c, Buffer * buf) override {
        Communicator::Observer::update(c, buf);
    }

    IProducer<Data>* _producer;
    std::vector<Request> requests;
};

#endif
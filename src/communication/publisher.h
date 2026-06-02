#ifndef PUBLISHER_H
#define PUBLISHER_H

#include "communicator.h"
#include "iproducer.h"

template <typename Channel, typename Data>
class Publisher: public Communicator<Channel>{
public:
    typedef typename Channel::Buffer Buffer;

    Publisher(IProducer<Data>* producer):
        _producer(producer)
    {

    }
    
private::
    // when a INTEREST comes, register it (interval), should only receive Interest
    void update(typename Channel::Observer::Observing_Condition c, Buffer * buf) override {
        Communicator::Observer::update(c, buf);
    }

    IProducer<Data>* _producer;
};

#endif
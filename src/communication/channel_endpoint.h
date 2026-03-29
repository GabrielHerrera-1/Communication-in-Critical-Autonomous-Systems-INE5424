#ifndef CHANNEL_ENDPOINT_H
#define CHANNEL_ENDPOINT_H

#include "communication_endpoint.h"
#include "communicator.h"

// Adapter que expoe um Communicator<Channel> pela interface generica
// Communication_Endpoint.
template <typename Channel>
class Channel_Endpoint : public Communication_Endpoint {
public:
    typedef typename Channel::Address Address;

    Channel_Endpoint(Channel * channel, Address address)
        : _communicator(channel, address) {}

    bool send(const Message * message) override {
        return _communicator.send(message);
    }

    bool receive(Message * message) override {
        return _communicator.receive(message);
    }

private:
    Communicator<Channel> _communicator;
};

#endif

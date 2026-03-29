#include "component.h"

Component::Component(const std::string& id)
    : _id(id),
      _port(0),
      _endpoint{}
{}

Component::~Component() {}

const std::string& Component::id() const {
    return _id;
}

void Component::set_endpoint(Communication_Endpoint * endpoint) {
    _endpoint = endpoint;
}

void Component::set_port(Port port) {
    _port = port;
}

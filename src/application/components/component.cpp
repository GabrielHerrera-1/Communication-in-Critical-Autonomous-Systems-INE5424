#include "component.h"

Component::Component(const std::string& id)
    : _id(id),
      _port(0),
      _communicator{}
{}

Component::~Component() {}

const std::string& Component::id() const {
    return _id;
}

void Component::set_communicator(Communicator<Vehicle_Protocol> * communicator) {
    _communicator = communicator;
}

void Component::set_port(Port port) {
    _port = port;
}

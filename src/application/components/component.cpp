#include "component.h"

Component::Component(const std::string& id)
    : _id(id) {}

Component::~Component() {}

const std::string& Component::id() const {
    return _id;
}

#include "component.hpp"
#include <iostream>

Component::Component(const std::string& id)
    : component_id(id){}

Component::~Component() {}

const std::string Component::get_id() const {
    return component_id;
}

SensorPayload Component::serialize_sensor_data(int32_t value) const {
    return {get_component_type(), value};
}
#include "actuator.h"

Actuator::~Actuator() {}

ActuatorPayload Actuator::serialize_actuator_response(double target_value, int32_t status) const {
    return {get_component_type(), target_value, status};
}

#ifndef COMPONENT_H
#define COMPONENT_H

#include <string>
#include <cstddef>
#include "component_types.h"

class Component {
protected:
    std::string component_id;

public:
    Component(const std::string& id);
    virtual ~Component();

    virtual void initialize() = 0;
    virtual void run() = 0;

    const std::string get_id() const;
    
    // Métodos para integração com rede/serialização
    virtual ComponentType get_component_type() const = 0;
    virtual SensorPayload serialize_sensor_data(int32_t value) const;
};

#endif

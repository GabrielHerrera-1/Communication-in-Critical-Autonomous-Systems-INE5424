#ifndef GATEWAY_H
#define GATEWAY_H

#include "components/component.h"
#include "vehicle_protocol.h"
#include "../network/engine/shared_memory_engine.h"

#include <memory>

class Gateway : public Component {
public:
    Gateway();
    ~Gateway();

    SharedMemoryEngine::Context create_context(const uint16_t * ports,
                                               unsigned int component_count) const;
    void set_context(const SharedMemoryEngine::Context & context);
    void set_master(bool is_master) { _is_master = is_master; }

    void initialize() override;
    void run() override;
    Port logical_port() const override;
    void stop();

private:
    SharedMemoryEngine::Context _context;
    bool _is_master = false;
    std::unique_ptr<Vehicle_Protocol> _protocol;
};

#endif

#include "gateway.h"

#include <cstdio>
#include <cstdlib>

namespace {

} // namespace

Gateway::Gateway()
    : Component("gateway"),
      _context{-1, -1} {}

Gateway::~Gateway() {
    stop();
}

SharedMemoryEngine::Context Gateway::create_context(const uint16_t * ports,
                                                    unsigned int component_count) const {
    return SharedMemoryEngine::create(ports, component_count);
}

void Gateway::set_context(const SharedMemoryEngine::Context & context) {
    _context = context;
}

void Gateway::initialize() {}

Component::Port Gateway::logical_port() const {
    return Component_Ports::GATEWAY;
}

void Gateway::run() {
    if (_context.shmid < 0 || _context.semid < 0) {
        return;
    }

    SharedMemoryEngine::Configuration gateway_config = {};
    gateway_config.context = _context;
    gateway_config.slot = SHM::GATEWAY_SLOT;
    gateway_config.port = logical_port();
    SharedMemoryEngine::configure(gateway_config);

    _protocol = std::make_unique<Vehicle_Protocol>();

    _protocol->enable_sync(_is_master);
}

void Gateway::stop() {
    _protocol.reset();
}

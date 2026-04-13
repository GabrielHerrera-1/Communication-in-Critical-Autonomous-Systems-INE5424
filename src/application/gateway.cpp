#include "gateway.h"

#include <csignal>

namespace {

void gateway_wakeup_handler(int) {}

}

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

    // O gateway espera a NIC de rede via select() e precisa ser acordado
    // quando um componente publica algo para ele na SHM. O handler nao sobe a
    // pilha; ele so interrompe o select() para o loop drenar o que estiver
    // pendente em contexto normal.
    struct sigaction sa{};
    sa.sa_handler = gateway_wakeup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, nullptr);

    SharedMemoryEngine::Configuration gateway_config = {};
    gateway_config.context = _context;
    gateway_config.slot = SHM::GATEWAY_SLOT;
    gateway_config.port = logical_port();
    SharedMemoryEngine::configure(gateway_config);

    _protocol = std::make_unique<Vehicle_Protocol>();
}

bool Gateway::dispatch_events(bool block, int timeout_ms) {
    if (!_protocol) {
        return false;
    }

    return _protocol->dispatch(block, timeout_ms);
}

void Gateway::stop() {
    _protocol.reset();
}

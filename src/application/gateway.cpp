#include "gateway.h"

#include <cstdio>
#include <cstdlib>

namespace {

// le o MAC do master PTP de SO2_SPTP_MASTER_MAC. TODO: ver se isso é ok ou temos que ter mac dinamico msm
Ethernet::Address resolve_master_paddr() {
    const char * env = std::getenv("SO2_SPTP_MASTER_MAC");
    if (env && *env) {
        unsigned int b[6];
        if (std::sscanf(env, "%x:%x:%x:%x:%x:%x",
                        &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6 &&
            b[0] < 256 && b[1] < 256 && b[2] < 256 &&
            b[3] < 256 && b[4] < 256 && b[5] < 256) {
            return Ethernet::Address(
                static_cast<uint8_t>(b[0]), static_cast<uint8_t>(b[1]),
                static_cast<uint8_t>(b[2]), static_cast<uint8_t>(b[3]),
                static_cast<uint8_t>(b[4]), static_cast<uint8_t>(b[5]));
        }
        std::fprintf(stderr,
            "[Gateway] SO2_SPTP_MASTER_MAC invalido (\"%s\"); usando fallback "
            "52:54:00:12:34:01.\n", env);
    }
    return Ethernet::Address(0x52, 0x54, 0x00, 0x12, 0x34, 0x01);
}

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

    // o mac do master ptp vem de SO2_SPTP_MASTER_MAC (com fallback pra VM1
    // do QEMU). se o mac do proprio gateway bater com o resolvido, o SPTP
    // detecta own_addr == master_addr e assume o papel de master. senao,
    // vira slave 
    Vehicle_Protocol::Address master_addr(resolve_master_paddr(), 0);
    _protocol->enable_sync(master_addr);
}

void Gateway::stop() {
    _protocol.reset();
}

#ifndef GPS_H
#define GPS_H

#include <cstdint>

// numeros dos ioctls compartilhados com o modulo de kernel (kernel/gps_module)
#include "../../kernel/gps_module/gps_ioctl.h"

// wrapper userspace do modulo de kernel

// abre /dev/gps e consulta o quadrante da vm via ioctl. 
// vive no processo gateway, junto da nic raw socket: a
// nic que pergunta em qual quadrante estou a cada envio
//
// o quadrante é estado global do modulo de kernel, entao todos os processos
// da vm (gateway e componentes) leem o mesmo valor --> os componentes de um
// mesmo sistema compartilham a percepcao do espaco
//
// QUADRANT_NONE = GPS indisponivel (ex: build nativo sem o modulo carregado)
class GPS {
public:
    static const uint8_t QUADRANT_NONE = 0xFF;

    GPS();
    ~GPS();

    // true se /dev/gps foi aberto com sucesso
    bool ok() const { return _fd >= 0; }

    // rsu e o unico com is_master=true: congela o quadrante no modulo de
    // kernel (ioctl GPS_IOC_SET_FIXED). como o estado e global ao modulo, o
    // congelamento vale para todos os processos da vm
    void set_fixed(bool fixed);

    // quadrante atual da VM (0..3), ou QUADRANT_NONE se o GPS estiver
    // indisponivel. avanca a simulacao de deslocamento no kernel se ja
    // passou o intervalo
    uint8_t quadrant();

private:
    int _fd;
};

#endif

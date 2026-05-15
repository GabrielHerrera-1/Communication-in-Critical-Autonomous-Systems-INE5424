#ifndef GPS_H
#define GPS_H

#include <cstdint>

// Numeros dos ioctls compartilhados com o modulo de kernel (kernel/gps_module).
#include "../../kernel/gps_module/gps_ioctl.h"

// Wrapper userspace do modulo de kernel GPS.
//
// Abre /dev/gps e consulta o quadrante da VM via ioctl (open/close/ioctl,
// sem read/write). Vive no processo gateway, junto da NIC de raw socket: e a
// NIC quem pergunta "em qual quadrante estou" a cada envio.
//
// O quadrante e estado global do modulo de kernel, entao todos os processos
// da VM (gateway e componentes) leem o mesmo valor -- os componentes de um
// mesmo sistema compartilham a percepcao do espaco.
//
// QUADRANT_NONE = GPS indisponivel (ex.: build nativo sem o modulo carregado)
// -> a NIC trata isso como "sem filtragem espacial".
class GPS {
public:
    static const uint8_t QUADRANT_NONE = 0xFF;

    GPS();
    ~GPS();

    // true se /dev/gps foi aberto com sucesso
    bool ok() const { return _fd >= 0; }

    // RSU e o unico no com is_master=true: congela o quadrante no modulo de
    // kernel (ioctl GPS_IOC_SET_FIXED). Como o estado e global ao modulo, o
    // congelamento vale para todos os processos da VM. set_fixed(false) e
    // no-op (estado padrao do modulo ja e "se desloca").
    void set_fixed(bool fixed);

    // quadrante atual da VM (0..3), ou QUADRANT_NONE se o GPS estiver
    // indisponivel. Avanca a simulacao de deslocamento no kernel se ja
    // passou o intervalo (a menos que o quadrante tenha sido congelado).
    uint8_t quadrant();

private:
    int _fd;
};

#endif

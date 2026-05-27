#include "gps.h"

// syscalls linux
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <cstdio>

// inicializa _fd como -1 (invalido, nenhum device aberto)
GPS::GPS() : _fd(-1) {
    // gps indisponivel (modulo nao carregado): ok() retorna false e a nic
    // desabilita filtragem espacial. nao imprime erro pois e comportamento
    // esperado nos cenarios sem o modulo gps
    _fd = ::open("/dev/gps", O_RDWR);
}

GPS::~GPS() {
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

// wrapper do ioctl set fixed
void GPS::set_fixed(bool fixed) {
    if (!fixed || _fd < 0) {
        return; // estado padrao do modulo ja e "se desloca"
    }
    if (::ioctl(_fd, GPS_IOC_SET_FIXED) < 0) {
        perror("[GPS] ioctl GPS_IOC_SET_FIXED");
    }
}

// wrapper do ioctl get quadrant
uint8_t GPS::quadrant() {
    if (_fd < 0) {
        return QUADRANT_NONE;
    }
    int q = 0;
    if (::ioctl(_fd, GPS_IOC_GET_QUADRANT, &q) < 0) {
        perror("[GPS] ioctl GPS_IOC_GET_QUADRANT");
        return QUADRANT_NONE;
    }
    return static_cast<uint8_t>(q);
}

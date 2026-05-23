#include "gps.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>

GPS::GPS() : _fd(-1) {
    // GPS indisponivel (modulo nao carregado): ok() retorna false e a NIC
    // desabilita filtragem espacial. Nao imprime erro pois e comportamento
    // esperado nos cenarios sem o modulo GPS.
    _fd = ::open("/dev/gps", O_RDWR);
}

GPS::~GPS() {
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

void GPS::set_fixed(bool fixed) {
    if (!fixed || _fd < 0) {
        return; // estado padrao do modulo ja e "se desloca"
    }
    if (::ioctl(_fd, GPS_IOC_SET_FIXED) < 0) {
        perror("[GPS] ioctl GPS_IOC_SET_FIXED");
    }
}

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

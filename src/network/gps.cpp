#include "gps.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>

GPS::GPS() : _fd(-1) {
    _fd = ::open("/dev/gps", O_RDWR);
    if (_fd < 0) {
        // GPS indisponivel (build nativo / modulo nao carregado): a NIC vai
        // tratar isso como "sem filtragem espacial".
        perror("[GPS] open /dev/gps");
    }
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

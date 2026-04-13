#include "raw_socket_engine.h"

#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <cstdio>
#include <fcntl.h>

void RawSocketEngine::engine_init(const char* iface) {
    _sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (_sockfd < 0) { perror("[Engine] socket"); return; }

    _ifindex = if_nametoindex(iface);
    if (_ifindex == 0) {
        perror("[Engine] interface");
        close(_sockfd);
        _sockfd = -1;
        return;
    }

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family   = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex  = _ifindex;

    if (bind(_sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[Engine] bind");
        close(_sockfd);
        _sockfd = -1;
        _ifindex = 0;
    }
}

int RawSocketEngine::engine_send(const void* frame, unsigned int size) {
    if (_sockfd < 0 || _ifindex == 0) return -1;

    struct sockaddr_ll dest;
    memset(&dest, 0, sizeof(dest));
    dest.sll_family  = AF_PACKET;
    dest.sll_ifindex = _ifindex;
    dest.sll_halen   = 6;
    memcpy(dest.sll_addr, frame, 6);

    return sendto(_sockfd, frame, size, 0,
                  (struct sockaddr*)&dest, sizeof(dest));
}

int RawSocketEngine::engine_receive(void * frame, unsigned int size) {
    if (_sockfd < 0 || !frame || size == 0) {
        return -1;
    }

    return static_cast<int>(recv(_sockfd, frame, size, 0));
}

void RawSocketEngine::engine_close() {
    if (_sockfd >= 0) {
        close(_sockfd);
        _sockfd = -1;
    }
}

void RawSocketEngine::engine_get_address(unsigned char* mac) {
    if (!mac) return;
    memset(mac, 0, 6);
    if (_sockfd < 0 || _ifindex == 0) return;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    char name[IF_NAMESIZE];
    if (!if_indextoname(_ifindex, name)) {
        perror("[Engine] if_indextoname");
        return;
    }
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(_sockfd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("[Engine] ioctl");
        return;
    }
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
}

void RawSocketEngine::engine_set_nonblocking(bool enabled) {
    if (_sockfd < 0) {
        return;
    }

    int fl = fcntl(_sockfd, F_GETFL, 0);
    if (fl < 0) {
        perror("[Engine] fcntl(F_GETFL)");
        return;
    }

    if (enabled) {
        fl |= O_NONBLOCK;
    } else {
        fl &= ~O_NONBLOCK;
    }

    if (fcntl(_sockfd, F_SETFL, fl) < 0) {
        perror("[Engine] fcntl(F_SETFL)");
    }
}

int RawSocketEngine::engine_wait_descriptor() const {
    return _sockfd;
}

// coloquei aqui porque senao dropa tudo na nic quando é ipc
bool RawSocketEngine::engine_should_drop_frame(const Ethernet::Frame & frame,
                                               const Ethernet::Address & local_address) const {
    return frame.src() == local_address;
}

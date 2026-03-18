#ifndef ENGINE_H
#define ENGINE_H

#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

class RawSocket_Engine {
protected:
    // O construtor faz o setup do socket cru
    RawSocket_Engine() : _sockfd(-1) {
        _sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (_sockfd < 0) return;

        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
        ioctl(_sockfd, SIOCGIFINDEX, &ifr);
        _ifindex = ifr.ifr_ifindex;

        ioctl(_sockfd, SIOCGIFHWADDR, &ifr);
        std::memcpy(_mac_address, ifr.ifr_hwaddr.sa_data, 6);

        struct sockaddr_ll addr = {};
        addr.sll_family = AF_PACKET;
        addr.sll_protocol = htons(ETH_P_ALL);
        addr.sll_ifindex = _ifindex;
        bind(_sockfd, (struct sockaddr*)&addr, sizeof(addr));
    }

    ~RawSocket_Engine() {
        if (_sockfd >= 0) close(_sockfd);
    }

    // Função burra e genérica: envia N bytes para a rede
    int engine_send(const void* data, unsigned int size) {
        struct sockaddr_ll dest = {};
        dest.sll_ifindex = _ifindex;
        return sendto(_sockfd, data, size, 0, (struct sockaddr*)&dest, sizeof(dest));
    }

    // Função burra e genérica: escuta N bytes da rede
    int engine_receive(void* buffer, unsigned int size) {
        return recv(_sockfd, buffer, size, 0);
    }

    // Devolve o MAC Address físico desta máquina
    void engine_get_mac(unsigned char* mac) const {
        std::memcpy(mac, _mac_address, 6);
    }

private:
    int _sockfd;
    int _ifindex;
    unsigned char _mac_address[6];
};

#endif // ENGINE_H

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
#include <signal.h>
#include <sys/syscall.h>

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

void RawSocketEngine::engine_close() {
    _running = false;
    if (_sockfd >= 0) { close(_sockfd); _sockfd = -1; }
    if (_worker.joinable()) _worker.join();
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

void RawSocketEngine::start_receiving() {
    if (_sockfd < 0) return;
    _running = true;

    // seta socket como non-blocking + O_ASYNC (habilita SIGIO)
    int fl = fcntl(_sockfd, F_GETFL, 0);
    fcntl(_sockfd, F_SETFL, fl | O_NONBLOCK | O_ASYNC);

    _worker = std::thread([this]() {
        // registra essa thread pra receber SIGIO do socket
        struct f_owner_ex owner;
        owner.type = F_OWNER_TID;
        owner.pid = static_cast<pid_t>(syscall(SYS_gettid));
        fcntl(_sockfd, F_SETOWN_EX, &owner);

        // ignora SIGIO como ação default (vamos usar sigtimedwait)
        struct sigaction sa{};
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGIO, &sa, nullptr);

        // bloqueia SIGIO pra poder esperar com sigtimedwait
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGIO);
        pthread_sigmask(SIG_BLOCK, &mask, nullptr);

        unsigned char buf[1514];

        while (_running) {
            // bloqueia esperando SIGIO (signal de que chegou dado no socket)
            struct timespec ts;
            ts.tv_sec = 1;
            ts.tv_nsec = 0;

            siginfo_t si{};
            int sig = sigtimedwait(&mask, &si, &ts);
            if (sig < 0) {
                if (errno == EAGAIN) continue; // timeout, checa _running
                break;
            }

            // SIGIO recebido: drena todos os pacotes pendentes (non-blocking)
            while (_running) {
                ssize_t n = recv(_sockfd, buf, sizeof(buf), 0);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    break;
                }
                if (n > 0 && _on_receive) {
                    _on_receive(buf, static_cast<size_t>(n));
                }
            }
        }
    });
}

// coloquei aqui porque senao dropa tudo na nic quando é ipc
bool RawSocketEngine::engine_should_drop_frame(const Ethernet::Frame & frame,
                                               const Ethernet::Address & local_address) const {
    return frame.src() == local_address;
}

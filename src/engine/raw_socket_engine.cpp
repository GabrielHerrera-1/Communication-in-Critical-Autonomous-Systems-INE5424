#include "engine/raw_socket_engine.h"

#include <sys/socket.h> // socket(), bind(), sendto(), recv() — operacoes basicas de rede do linux (como fopen/fread mas pra rede)
#include <linux/if_packet.h> // struct sockaddr_ll e AF_PACKET — especifico do linux pra acesso a frames ethernet crus. "ll" = link layer
#include <net/ethernet.h> // ETH_P_ALL (0x0003) — constante que significa "qualquer protocolo ethernet"
#include <net/if.h> // if_nametoindex(), if_indextoname(), struct ifreq — funcoes pra lidar com interfaces de rede. "if" = interface
#include <sys/ioctl.h> // ioctl() e SIOCGIFHWADDR — faz perguntas genericas pro kernel sobre dispositivos
#include <unistd.h> // close() — fecha file descriptors (arquivos, sockets, pipes)
#include <cstring> // memset(), memcpy(), strncpy() — operacoes em blocos de memoria
#include <arpa/inet.h> // htons() — "Host TO Network Short". converte uint16 de little-endian (x86/risc-v) pra big-endian (padrao da rede)
#include <cstdio> // perror() — imprime a mensagem de erro da ultima chamada de sistema que falhou

// abre o socket cru, descobre o indice da interface e faz bind nela
// recebe o nome da interface (ex: "eth0") que vira de Traits<NIC>::INTERFACE
void RawSocketEngine::engine_init(const char* iface) {
    // socket() é como fopen() pra rede. retorna um file descriptor (inteiro que representa o socket aberto)
    // AF_PACKET = acesso direto a camada 2 (ethernet), sem IP. se usassemos AF_INET, o kernel trataria IP/TCP/UDP pra nos
    // SOCK_RAW = queremos o frame completo incluindo header ethernet (dst+src+type+payload)
    // ETH_P_ALL = recebe frames de qualquer protocolo. htons() converte pra big-endian (padrao da rede)
    _sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (_sockfd < 0) { perror("[Engine] socket"); return; }

    // o kernel identifica interfaces por numero, nao por nome. converte "eth0" -> 2 (ou qualquer indice)
    _ifindex = if_nametoindex(iface);
    // no caso de a interface não existir por exemplo
    if (_ifindex == 0) {
        perror("[Engine] interface");
        close(_sockfd);
        _sockfd = -1;
        return;
    }

    // sockaddr_ll = struct que descreve "onde" um socket de camada 2 se conecta. "ll" = link layer, camada de enlace
    // memset zera tudo antes de preencher. sem isso os campos que nao preenchemos poderiam ter lixo de memoria
    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family   = AF_PACKET; // familia do socket (sempre AF_PACKET aqui)
    addr.sll_protocol = htons(ETH_P_ALL); // quais protocolos receber
    addr.sll_ifindex  = _ifindex; // qual interface (eth0)
    // bind = "amarra esse socket nessa interface". sem bind, receberia frames de TODAS as interfaces
    // o cast (struct sockaddr*) existe porque bind aceita varios tipos de endereco (ipv4, ipv6, packet...)
    // o tipo generico é struct sockaddr, e o kernel olha o campo family pra saber qual é
    
    // no caso de o bind falhar
    if (bind(_sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[Engine] bind");
        close(_sockfd);
        _sockfd = -1;
        _ifindex = 0;
    }
}

// envia o frame completo (header ethernet + payload) pro socket.
// a NIC monta o frame (dst+src+type+payload) e passa pra ca. o engine so empurra pro kernel
int RawSocketEngine::engine_send(const void* frame, unsigned int size) {
    // no caso de o engine_init ter falhado em qualquer ponto
    if (_sockfd < 0 || _ifindex == 0) {
        return -1;
    }

    // monta o sockaddr_ll de destino pro sendto
    struct sockaddr_ll dest;
    memset(&dest, 0, sizeof(dest));
    dest.sll_family  = AF_PACKET;
    dest.sll_ifindex = _ifindex; // por qual interface sair
    dest.sll_halen   = 6; // tamanho do MAC address (6 bytes)
    // copia os primeiros 6 bytes do frame pro sockaddr_ll. por que? porque no layout ethernet:
    // byte 0-5 = MAC destino, byte 6-11 = MAC origem, byte 12-13 = ethertype, byte 14+ = payload
    // o sendto precisa do MAC destino aqui pra saber pra onde enviar
    memcpy(dest.sll_addr, frame, 6);
    // sendto = send + "pra quem". o kernel adiciona preambulo, SFD e FCS automaticamente
    return sendto(_sockfd, frame, size, 0,
                  (struct sockaddr*)&dest, sizeof(dest));
}

// bloqueia a thread ate chegar um frame da rede.
// o kernel ja tirou preambulo/SFD/FCS. o que chega é: [dst 6B][src 6B][type 2B][payload ate 1500B]
// retorna quantos bytes leu. se o socket for fechado (engine_close), retorna -1 e a thread da NIC sai do loop
int RawSocketEngine::engine_receive(void* frame, unsigned int size) {
    // no caso de o socket nao estar aberto
    if (_sockfd < 0) {
        return -1;
    }
    return recv(_sockfd, frame, size, 0);
}

// fecha o socket. seta -1 como sentinela pra nao fechar duas vezes.
// o close() tambem desbloqueia qualquer recv() pendente (a thread da NIC sai do loop)
void RawSocketEngine::engine_close() {
    if (_sockfd >= 0) { close(_sockfd); _sockfd = -1; }
}

// pergunta pro kernel qual o MAC da interface e copia pro ponteiro que a NIC passou.
// a NIC chama isso no construtor dela pra saber o proprio endereco
void RawSocketEngine::engine_get_address(unsigned char* mac) {
    if (!mac) {
        return;
    }

    // zera o mac antes de tentar ler. se qqr coisa falhar depois o chamador recebe um mac zearado, não lixo de memoria
    memset(mac, 0, 6);
    
    // se o engine n tiver sido inicializado corretamente n tenta fazer o ioctl
    if (_sockfd < 0 || _ifindex == 0) {
        return;
    }

    // ifreq = "interface request". struct usada pelo ioctl pra perguntas sobre interfaces
    // campos principais: ifr_name (entrada, nome da interface), ifr_hwaddr (saida, MAC preenchido pelo kernel)
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    // ioctl identifica interface pelo nome, nao pelo numero. converte de volta: 2 -> "eth0"
    char name[IF_NAMESIZE];
    if (!if_indextoname(_ifindex, name)) {
        perror("[Engine] if_indextoname");
        return;
    }
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1); // IFNAMSIZ = 16 bytes, tamanho maximo de nome de interface no linux
    // ioctl = canivete suico do linux. o segundo argumento diz O QUE perguntar:
    // SIOCGIFHWADDR = "Socket IO Control Get InterFace HardWare ADDRess" = me da o MAC dessa interface
    if (ioctl(_sockfd, SIOCGIFHWADDR, &ifr) < 0)
        { perror("[Engine] ioctl"); return; }
    // ifr_hwaddr é um struct sockaddr. o MAC fica dentro de sa_data. copia os 6 bytes pro ponteiro da NIC
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
}

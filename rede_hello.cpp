#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>

int main() {
    // 1. Criar o Raw Socket (Acesso direto à camada de enlace) 
    int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)); // 
    if (sockfd < 0) {
        std::cerr << "Erro ao criar socket. Tem permissão de root?" << std::endl;
        return 1;
    }

    // 2. Descobrir o índice da placa de rede (eth0)
    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "Erro ao pegar indice da eth0" << std::endl;
        return 1;
    }
    int ifindex = ifr.ifr_ifindex;

    // 3. Descobrir o MAC Address dessa VM 
    if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0) { // 
        std::cerr << "Erro ao pegar MAC" << std::endl;
        return 1;
    }
    unsigned char my_mac[6];
    std::memcpy(my_mac, ifr.ifr_hwaddr.sa_data, 6);

    // 4. Fazer o Bind (Ancorar o socket apenas na eth0) 
    struct sockaddr_ll addr = {};
    addr.sll_family = AF_PACKET; // 
    addr.sll_protocol = htons(ETH_P_ALL); // 
    addr.sll_ifindex = ifindex;
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { // 
        std::cerr << "Erro no bind" << std::endl;
        return 1;
    }

    std::cout << "[INFO] Socket Raw aberto e linkado na eth0 com sucesso!" << std::endl;

    // 5. Montando o frame Ethernet manual 
    unsigned char frame[1500] = {0};
    
    // Dst MAC: Broadcast FF:FF:FF:FF:FF:FF 
    std::memset(frame, 0xFF, 6); 
    // Src MAC: O MAC da nossa própria VM
    std::memcpy(frame + 6, my_mac, 6);
    // EtherType: 0x8888 (O protocolo customizado exigido pelo projeto) 
    frame[12] = 0x88;
    frame[13] = 0x88;
    
    // Payload (A mensagem em si) 
    const char* msg = "Grito na rede virtual!";
    std::memcpy(frame + 14, msg, std::strlen(msg));

    int frame_len = 14 + std::strlen(msg);

    // 6. Enviar a mensagem para a rede 
    struct sockaddr_ll dest = {};
    dest.sll_ifindex = ifindex;
    if (sendto(sockfd, frame, frame_len, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) { // 
        std::cerr << "Erro ao enviar" << std::endl;
    } else {
        std::cout << "[TX] Mensagem enviada em broadcast para todos os carros!" << std::endl;
    }

    // 7. Ficar escutando a rede para ver se alguém responde 
    std::cout << "\n[RX] Escutando a rede virtual..." << std::endl;
    unsigned char recv_buf[1500];
    
    while (true) {
        int n = recv(sockfd, recv_buf, sizeof(recv_buf), 0); // 
        if (n > 0) {
            // Verifica se o pacote que chegou é do nosso protocolo 0x8888 
            if (recv_buf[12] == 0x88 && recv_buf[13] == 0x88) {
                // E evita ler a nossa própria mensagem (eco) 
                if (std::memcmp(recv_buf + 6, my_mac, 6) != 0) { 
                    std::cout << ">>> RECEBI UMA MENSAGEM! Tamanho: " << n << " bytes" << std::endl;
                    std::cout << ">>> Payload: " << (char*)(recv_buf + 14) << "\n" << std::endl;
                }
            }
        }
    }

    close(sockfd); // 
    return 0;
}
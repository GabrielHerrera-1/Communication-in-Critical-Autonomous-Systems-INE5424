#ifndef RAW_SOCKET_ENGINE_H
#define RAW_SOCKET_ENGINE_H

// header só com declarações. implementação fica no .cpp pra evitar recompilar tudo quando mudar algo do socket
class RawSocketEngine {
protected:
    // explicita construtor padrao
    RawSocketEngine() = default;
    // inicializa o socket cru e faz bind na interface. recebe o nome da interface (ex: "eth0") que vem de Traits
    void engine_init(const char* interface_name);
    // envia N bytes para a rede. a NIC monta o frame e passa pra ca
    int  engine_send(const void* frame, unsigned int size);
    // escuta N bytes da rede. bloqueia ate chegar algo
    int  engine_receive(void* frame, unsigned int size);
    // fecha o socket. chamado pelo destrutor da NIC
    void engine_close();
    // pega o MAC da interface via ioctl e copia pro ponteiro. a NIC usa pra saber o proprio endereço
    void engine_get_address(unsigned char* mac);
    // integra o socket com o event loop do gateway
    int engine_fd() const;
    void engine_set_nonblocking(bool enabled);

private:
    // valores padrao de inicializacao pra nao ter lixo de memoria
    int _sockfd{-1};   // file descriptor do socket cru
    int _ifindex{0};   // indice da interface de rede (ex: eth0 = 2)
};

#endif

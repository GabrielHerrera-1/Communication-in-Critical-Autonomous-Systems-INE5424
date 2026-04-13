#ifndef RAW_SOCKET_ENGINE_H
#define RAW_SOCKET_ENGINE_H

#include "../ethernet.h"

class RawSocketEngine {
protected:
    RawSocketEngine() = default;
    // inicializa o socket cru e faz bind na interface
    void engine_init(const char* interface_name);
    // envia N bytes para a rede
    int  engine_send(const void* frame, unsigned int size);
    // recebe um frame completo do kernel
    int engine_receive(void * frame, unsigned int size);
    // fecha o socket
    void engine_close();
    // pega o MAC da interface via ioctl
    void engine_get_address(unsigned char* mac);
    // a NIC controla se quer recv bloqueante ou non-blocking a cada dispatch
    void engine_set_nonblocking(bool enabled);
    // exposto para o loop do gateway esperar dados da rede com select()
    int engine_wait_descriptor() const;
    // na ethernet, dropamos o que veio com o mesmo MAC da NIC local
    bool engine_should_drop_frame(const Ethernet::Frame & frame,
                                  const Ethernet::Address & local_address) const;

private:
    int _sockfd{-1};
    int _ifindex{0};
};

#endif

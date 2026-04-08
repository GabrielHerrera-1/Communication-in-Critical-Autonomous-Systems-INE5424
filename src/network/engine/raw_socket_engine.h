#ifndef RAW_SOCKET_ENGINE_H
#define RAW_SOCKET_ENGINE_H

#include "../ethernet.h"

#include <functional>
#include <thread>
#include <atomic>

class RawSocketEngine {
protected:
    // callback chamado pela engine quando um frame chega.
    // a NIC registra esse callback no construtor dela
    typedef std::function<void(const unsigned char*, size_t)> ReceiveHandler;
    void set_receive_handler(ReceiveHandler handler) { _on_receive = handler; }

    RawSocketEngine() = default;
    // inicializa o socket cru e faz bind na interface
    void engine_init(const char* interface_name);
    // envia N bytes para a rede
    int  engine_send(const void* frame, unsigned int size);
    // fecha o socket e para a thread de recepção
    void engine_close();
    // pega o MAC da interface via ioctl
    void engine_get_address(unsigned char* mac);
    // inicia recepção via SIGIO + sigtimedwait.
    // a thread bloqueia esperando o signal, depois drena tudo non-blocking
    void start_receiving();
    // na ethernet, dropamos o que veio com o mesmo MAC da NIC local
    bool engine_should_drop_frame(const Ethernet::Frame & frame,
                                  const Ethernet::Address & local_address) const;

private:
    ReceiveHandler _on_receive;
    int _sockfd{-1};
    int _ifindex{0};
    std::thread _worker;
    std::atomic<bool> _running{false};
};

#endif

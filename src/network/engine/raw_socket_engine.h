#ifndef RAW_SOCKET_ENGINE_H
#define RAW_SOCKET_ENGINE_H

#include "../ethernet.h"
#include "../gps.h"

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
    // Etapa 4: dropamos o que veio com o mesmo MAC da NIC local E o que veio
    // de outro quadrante espacial (sincronizacao espacial por quadrantes).
    bool engine_should_drop_frame(const Ethernet::Frame & frame,
                                  const Ethernet::Address & local_address) const;

    // Etapa 4: quadrante espacial da VM, consultado ao modulo de kernel GPS.
    // A NIC carimba esse quadrante em todo frame que envia (alloc).
    uint8_t engine_current_quadrant() { return _gps.quadrant(); }
    // RSU (is_master=true) fixa o quadrante: nao se desloca.
    void engine_set_fixed(bool fixed) { _gps.set_fixed(fixed); }

private:
    ReceiveHandler _on_receive;
    int _sockfd{-1};
    int _ifindex{0};
    std::thread _worker;
    std::atomic<bool> _running{false};
    // mutable: engine_should_drop_frame e const mas precisa consultar o GPS
    mutable GPS _gps;
};

#endif

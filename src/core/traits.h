#ifndef TRAITS_H
#define TRAITS_H

// forward declarations (declarar antecipadamente evita a necessidade de dar #include)

#include <cstdint>

// T sera a engine da NIC (placa de rede)
template <typename T> class NIC;
class RawSocketEngine;
class SharedMemoryEngine;
// quando for usar Protocol precisa passar um tipo que represente uma NIC aqui dentro
template <typename SharedMemoryNIC, typename RawSocketNIC> class Protocol; 
// SPTP usa o Address do Protocol, forward declare pra poder especializar Traits
template <typename Address> class SPTP_Protocol;

// template generico vazio
template <typename T> struct Traits { };

// o que estou fazendo aqui é: quando eu iniciar uma placa de rede NIC baseada em raw socket
// eu quero essas configurações de numero de buffers e essa interface
// NIC --> quantos buffers e qual interface
template <> struct Traits<NIC<RawSocketEngine>> {
    static const unsigned int SEND_BUFFERS = 50;
    static const unsigned int RECEIVE_BUFFERS = 50;
    static constexpr const char* INTERFACE = "eth0";
};


// o que estou fazendo aqui é: quando eu iniciar uma camada de protocolo e ela estiver
// conectada a placa de rede NIC baseada em raw socket, eu quero que o ethertype seja 0x8888
// protocol --> qual ethertype usar (0x8888 é livre)
template <> struct Traits<Protocol<NIC<SharedMemoryEngine>,NIC<RawSocketEngine>>> {
    static const unsigned short ETHERNET_PROTOCOL_NUMBER = 0x8888;
};

// NIC de shared memory: mesmos buffers, sem interface de rede
template <> struct Traits<NIC<SharedMemoryEngine>> {
    static const unsigned int SEND_BUFFERS = 50;
    static const unsigned int RECEIVE_BUFFERS = 50;
    static constexpr const char* INTERFACE = nullptr;
};

// protocol sobre shared memory usa o mesmo ethertype
// (o header do Protocol viaja dentro do frame na SHM também)
template <> struct Traits<Protocol<NIC<SharedMemoryEngine>, void>> {
    static const unsigned short ETHERNET_PROTOCOL_NUMBER = 0x8888;
};

template <typename Address> struct Traits<SPTP_Protocol<Address>> {

    static constexpr double  MAX_SILENCE_S = 15.0;

    // usado no EWMA (delay_novo = (1 - 0.125) × delay_antigo + 0.125 × amostra_atual). serve basicamente pra diminuir o peso de amostras antigas
    // esse peso aqui é o peso de uma amostra nova
    static constexpr double  ALPHA = 0.125;

    static constexpr int64_t MIN_OFFSET_NS = 100'000;       // 100us
    // limite superior gera sobre quanto SPTP aceita corrigir num unico sync.
    // 1s e folgado o suficiente para absorver drift inicial em hosts lentos
    // (QEMU em maquina sob carga pode acumular >200ms antes do 1o sync).
    static constexpr int64_t MAX_OFFSET_NS = 1'000'000'000; // 1s

    // palpite inicial de delay, refinado em cada round-trip.
    static constexpr int64_t INITIAL_DELAY_NS = 100'000;     // 100us

    static constexpr unsigned INITIAL_RETRY_MS = 250;
};

#endif

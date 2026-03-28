// no app de teste a nic precisa ser incluida antes do protocol

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "../network/ethernet.h"
#include "../core/observers/conditional_data_observer.h"
#include "../core/observers/concurrent_observer.h"
#include "../core/buffer.h"
#include "../core/traits.h"
#include <cstring>

// Communication Protocol
// Protocol é generica e depende do tipo NIC, ja q ela vai chamar os metodos da NIC
template <typename NIC> class Protocol : private NIC::Observer { // herança privada do Observer que a NIC herdou
public:
    // numero do protocolo
    static const typename NIC::Protocol_Number PROTO = Traits<Protocol>::ETHERNET_PROTOCOL_NUMBER;
    // buffer - typedef pra nao precisar escrever Buffer<Ethernet::Frame> toda vez
    // ::Buffer referencia o template global
    typedef ::Buffer<Ethernet::Frame> Buffer;
    // MAC da NIC
    typedef typename NIC::Address Physical_Address;
    typedef uint16_t Port;
    static const Port BROADCAST_PORT = 0xFFFF;
    // a camada Protocol -> Communicator precisa ser concorrente porque o
    // Communicator bloqueia em receive() e acorda por semaforo.
    typedef Concurrent_Observer<Buffer, Port> Observer;
    typedef Concurrent_Observed<Buffer, Port> Observed;

    // address aqui é um endereco composto: mac + porta
    class Address {
    public:
        static const Address BROADCAST;
        // Null serve pra construir endereco nulo e comparar
        enum Null { NULL_ADDR };

    public:
        Address() : _paddr(), _port(0) {}
        // construtor que constroi um endereço nulo, usamos pra comparações
        Address(const Null &) : _paddr(), _port(0) {}
        // construtor padrao, combina MAC com porta
        Address(Physical_Address paddr, Port port) : _paddr(paddr), _port(port) {}
        // broadcast fisico sempre preserva a porta logica do componente
        static Address broadcast(Port port) { return Address(Ethernet::Address::BROADCAST, port); }

        // getters
        // nao coloquei getter pro paddr pra isolar o MAC
        Port port() const { return _port; }

        // retorna true se pelo menos um dos dois não for zero
        operator bool() const { return (_paddr || _port); }
        // dois end sao iguais se mac e porta batem
        bool operator==(const Address &a) const { return (_paddr == a._paddr) && (_port == a._port); }

    private:
        friend class Protocol<NIC>;
        Physical_Address _paddr;
        Port _port;
    };

    class Header {
    public:
        // getters e setters
        Port src_port() const { return _src_port; }
        void src_port(Port p) { _src_port = p; }
        Port dst_port() const { return _dst_port; }
        void dst_port(Port p) { _dst_port = p; }
    private:
        Port _src_port; // quem enviou
        Port _dst_port; // quem recebeu (vai ser broadcast por enquanto)
    };

    static const unsigned int MTU = NIC::MTU - sizeof(Header);
    typedef unsigned char Data[MTU];
    class Packet : public Header
    {
    public:
        Packet() = default;
        Header *header() { return this; }
        template <typename T>
        T *data() { return reinterpret_cast<T *>(&_data); }

    private:
        Data _data;
    } __attribute__((packed));

protected:
    Protocol(NIC *nic) : _nic(nic) {
        _nic->attach(this, PROTO);
        _instance = this;
    }

public:
    ~Protocol() { _nic->detach(this, PROTO); }
    
    static int send(Address from, Address to, const void *data, unsigned int size) {
        if (!_instance || !_instance->_nic) return -1;

        Buffer *buf = _instance->_nic->alloc(to._paddr, PROTO, sizeof(Header) + size);
        if (!buf) return -1;

        // pegar ponteiro pro payload do frame ethernet
        // reinterpret_cast --> trate esse endereço de memoria (buf->data()->payload(), que é *void) como se fosse um ponteiro pra Header
        // então eu pego o começo desse payload e escrevo o header
        // significa: pegue esse espaço vazio e trate o começo dele como um header
        Header *header = reinterpret_cast<Header*>(buf->data()->payload());
        header->src_port(from.port());
        header->dst_port(to.port());

        // copia os dados da aplicacao para logo depois do header na memoria
        // conversao pra char porque char anda de byte em byte, header anda em blocos de sizeof(Header)
        // + sizeof(Header) é aritmetica de ponteiro. vai pro espaço depois do header
        memcpy(reinterpret_cast<char*>(header) + sizeof(Header), data, size);

        return _instance->_nic->send(buf);
    };

    // me parece que o from deveria ser ponteiro, porque no codigo do communicator que o professor deu como base
    // ele chama receive com &from. por isso troquei aqui
    static int receive(Buffer *buf, Address *from, void *data, unsigned int size) {
        if (!_instance || !_instance->_nic || !buf) return -1;
        if (buf->size() < sizeof(Header)) {
            _instance->_nic->free(buf);
            return -1;
        }

        // aqui o communicator ja passou o buffer pro Protocol, vamos so desmontar esse buffer
        Ethernet::Address src_mac, dst_mac;
        _instance->_nic->unmarshal(buf, &src_mac, &dst_mac, nullptr, 0);

        Header *header = reinterpret_cast<Header*>(buf->data()->payload());
        if (from)
            *from = Address(src_mac, header->src_port());

        unsigned int data_size = buf->size() - sizeof(Header);
        if (data_size > size)
            data_size = size;
        if (data && data_size)
            memcpy(data, reinterpret_cast<char*>(header) + sizeof(Header), data_size);

        _instance->_nic->free(buf);
        return static_cast<int>(data_size);
    };

    // comentarios do guto abaixo. porem acredito que ele quisesse escrever NIC::unmarshal, porque o buffer ja foi recebido aqui,
    // caso façamos NIC::receive vamos receber outro pacote

    // unsigned int s = NIC::receive(buf, &from.paddr, &to.paddr, data, size)
    // NIC::free(buf)
    // return s;

    static void attach(Observer *obs, Address address) {
        _observed.attach(obs, address.port());
    };

    static void detach(Observer *obs, Address address) {
        _observed.detach(obs, address.port());
    };

private:
    // removemos o param obs porque não é usado
    void update(typename NIC::Protocol_Number prot, Buffer *buf)
    {

        if (!buf) return;
        if (buf->size() < sizeof(Header)) {
            _nic->free(buf);
            return;
        }

        Header *header = reinterpret_cast<Header*>(buf->data()->payload());
        
        bool notified = _observed.notify(header->dst_port(), buf);

        if (!notified) // to call receive(...);
            _nic->free(buf);
    }

private:
    NIC *_nic;
    // Channel protocols are usually singletons
    static Observed _observed;
    static Protocol* _instance; // ponteiro pro singleton
};

// inicializar BROADCAST
template <typename NIC>
const typename Protocol<NIC>::Address Protocol<NIC>::Address::BROADCAST(
    Ethernet::Address::BROADCAST, Protocol<NIC>::BROADCAST_PORT
);

// inicializacao dos static
template <typename NIC> Protocol<NIC>* Protocol<NIC>::_instance = nullptr;

template <typename NIC> typename Protocol<NIC>::Observed Protocol<NIC>::_observed;

#endif

// no app de teste a nic precisa ser incluida antes do protocol

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "../network/ethernet.h"
#include "../core/observers/conditional_data_observer.h"
#include "../core/observers/concurrent_observer.h"
#include "../core/buffer.h"
#include "../core/traits.h"
#include <cstring>
#include <type_traits>

// Communication Protocol
// Protocol tem uma nic interna e uma externa
// ta dando problema aqui por conta da declaração em traist.h , acredito que seja tranquilo de resolver
template <typename SharedMemoryNIC, typename RawSocketNIC = void>
class Protocol 
    : private SharedMemoryNIC::Observer,
    public Concurrent_Observed<Buffer<Ethernet::Frame>, uint16_t>
{
public:
    // numero do protocolo
    static const typename SharedMemoryNIC::Protocol_Number PROTO = Traits<Protocol>::ETHERNET_PROTOCOL_NUMBER;

    typedef ::Buffer<Ethernet::Frame> Buffer;

    typedef typename SharedMemoryNIC::Address Physical_Address;
    typedef uint16_t Port;
    typedef Concurrent_Observer<Buffer, Port> Observer;
    typedef Concurrent_Observed<Buffer, Port> Observed;

    // address aqui é um endereco composto: mac + porta
    class Address {
    public:
        // Null serve pra construir endereco nulo e comparar
        enum Null { NULL_ADDR };

    public:
        Address() : _paddr(), _port(0) {}
        // construtor que constroi um endereço nulo, usamos pra comparações
        Address(const Null &) : _paddr(), _port(0) {}
        // construtor padrao, combina MAC com porta
        Address(Physical_Address paddr, Port port) : _paddr(paddr), _port(port) {}
        // broadcast fisico sempre preserva a porta logica do endpoint
        static Address broadcast(Port port) { return Address(Ethernet::Address::BROADCAST, port); }

        static bool same_physical(const Address &addr1, const Address &addr2) const {
            return (addr1._paddr == addr2._paddr);
        }

        static const Physical_Address INTERNAL(0x00,0x00,0x00,0x00,0x00,0x00);

        bool is_internal(){
            return _paddr == INTERNAL;
        }

        Port port() const { return _port; }

        

        // retorna true se pelo menos um dos dois não for zero
        operator bool() const { return (_paddr || _port); }
        // dois end sao iguais se mac e porta batem
        bool operator==(const Address &a) const { return (_paddr == a._paddr) && (_port == a._port); }

    private:
        friend class Protocol<SharedMemoryNIC,RawSocketNIC>;
        Physical_Address _paddr;
        Port _port;
    };

    class Header {
    public:
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
        Header *header() { return static_cast<Header *>(this); }
        const Header *header() const { return static_cast<const Header *>(this); }
        template <typename T>
        T *data() { return reinterpret_cast<T *>(&_data); }
        template <typename T>
        const T *data() const { return reinterpret_cast<const T *>(&_data); }

    private:
        Data _data;
    } __attribute__((packed));

protected:
    
    Protocol(SharedMemoryNIC* shm_nic, uint8_t* mac_addr)
        : SharedMemoryNIC::Observer(),
         _shm_nic(shm_nic),
         _socket_nic(nullptr),
         _address(Physical_Address(mac_addr),0)
    {
        _shm_nic->attach(this, PROTO);
        _instance = this;
    }

    Protocol(SharedMemoryNIC* shm_nic, RawSocketNIC socket_nic, uint8_t* mac_addr )
        : SharedMemoryNIC::Observer(),
         _shm_nic(shm_nic),
         _socket_nic(socket_nic),
         _address(Physical_Address(mac_addr),0)
    {
        _shm_nic->attach(this,PROTO);
        _socket_nic->attach(this,PROTO);
        _instance = this
    }

public:
    ~Protocol() {

        _shm_nic->detach(this, PROTO); 
        if constexpr (!std::is_void_v<RawSocketNIC>){
            _socket_nic->detach(this, PROTO)
        }
        
    }
    
    // aqui talvez falta alguma coisa, talvez de endereçamento,
    // pra identificar se pode ser uma mensagem puramente interna ou se é externa
    // solução atual, se o mac do to 000000 significa local
    //
    static int send(Address from, Address to, const void *data, unsigned int size) {

        if (!_instance) return -1;
        
        // se tem raw socket manda em raw socket
        if constexpr (!std::is_void_v<RawSocketNIC>){
            if (_instance->_socket_nic && !to.is_internal()) {
                return typed_nic_send<RawSocketNIC>(_instance->_socket_nic, from, to, data, size);
            }
        }
        // se não usa shm
        else{
            return typed_nic_send<SharedMemoryEngine>(_instance->_shm_nic, from, to, data, size);
        }
        

        
    };


    // talvez tenha que ser um pouco diferente, utilizando um tipo de typed_nic_receive
    // isso pra englobar o caso onde a comunicação é direcionada diretamente ao gateway
    // (ou podemos invalidar esse caso)
    static int receive(Buffer *buf, Address *from, void *data, unsigned int size) {
        if (!_instance || !buf) return -1;
        if (buf->size() < sizeof(Header)) {
            _instance->_shm_nic->free(buf);
            return -1;
        }

        Packet *packet = reinterpret_cast<Packet *>(buf->data()->payload());
        if (from) {
            *from = Address(buf->data()->src(), packet->src_port());
        }

        unsigned int data_size = buf->size() - sizeof(Header);
        if (data_size > size) data_size = size;
        
        if (data && data_size)
            memcpy(data, packet->data(), data_size);

        _instance->_shm_nic->free(buf);
        return static_cast<int>(data_size);
    }

    Address create_address(Port port){
        return Address(_nic->address(),port);
    }

    static void attach(Observer *obs, Address address) {
        _observed.attach(obs, address.port());
    };

    static void detach(Observer *obs, Address address) {
        _observed.detach(obs, address.port());
    };

private:
    void update(typename SharedMemoryNIC::Protocol_Number prot, Buffer *buf)
    {
        if (!buf) return;
        if (buf->size() < sizeof(Header)) {
            _shm_nic->free(buf);
            return;
        }

        // GATEWAY FORWARDING:
        // If this instance has a socket and the frame's physical destination is not "itself"
        if constexpr (!std::is_void_v<RawSocketNIC>) {
            if (_socket_nic && !(Address::same_physical(buf->data()->src(),_address)) && !buf->data()->dst().is_broadcast()) {
                _socket_nic->send(buf);
                return; 
            }
        }

        Packet *packet = reinterpret_cast<Packet *>(buf->data()->payload());
        bool notified = _observed.notify(packet->dst_port(), buf);

        if (!notified)
            _shm_nic->free(buf);
    }

    // o frame é o da ethernet independente da nic

    template<typename NICType >
    void static typed_nic_send(NICType *nic, Address from, Address to, const void *data, unsigned int size){
        Buffer *buf = nic->alloc(to.paddr(), PROTO, sizeof(Header) + size);
        if (!buf) return -1;

        Packet *packet = reinterpret_cast<Packet *>(buf->data()->payload());
        packet->src_port(from.port());
        packet->dst_port(to.port());
        
        if (data && size)
            memcpy(packet->data<unsigned char>(), data, size);

        return nic->send(buf);
    }


private:
    SharedMemoryNIC *_shm_nic;
    RawSocketEngine *_socket_nic;
    Address _address;
    // Channel protocols are usually singletons
    static Observed _observed;
    static Protocol* _instance; // ponteiro pro singleton
};

// inicializacao dos static
/*

// nem lembro oque é isso aqui em baixo, tem que dar uma olhada dps

template <typename NIC> Protocol<NIC>* Protocol<NIC>::_instance = nullptr;

template <typename NIC> typename Protocol<NIC>::Observed Protocol<NIC>::_observed;

*/
#endif

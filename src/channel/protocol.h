// no app de teste a nic precisa ser incluida antes do protocol

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "../network/ethernet.h"
#include "../core/observers/conditional_data_observer.h"
#include "../core/observers/concurrent_observer.h"
#include "../core/buffer.h"
#include "../core/traits.h"
#include "../core/clock.h"
#include "packet_kind.h"
#include "sptp_protocol.h"
#include <cstring>
#include <limits>
#include <type_traits>
#include <iostream>

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
        static Address physical_broadcast(Port port) { return Address(Ethernet::Address::BROADCAST, port); }
        // O broadcast logico da biblioteca precisa ser um endereco explicito da
        // pilha, e nao um detalhe codificado dentro do Communicator. Assim cada
        // componente pode manter sua propria porta e ainda ouvir o grupo de
        // broadcast sem perder a identificacao do emissor em src_port.
        static Address logical_broadcast() { return physical_broadcast(broadcast_port()); }
        static constexpr Port broadcast_port() { return std::numeric_limits<Port>::max(); }

        static bool same_physical(const Address &addr1, const Address &addr2) {
            return (addr1._paddr == addr2._paddr);
        }

        static const Physical_Address& INTERNAL() {
            return Ethernet::Address::INTERNAL;
        }

        bool is_internal() const {
            return _paddr == INTERNAL();
        }

        const Physical_Address& paddr() const { return _paddr; }

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
        int64_t timestamp() const { return _timestamp; }
        void timestamp(int64_t ts) { _timestamp = ts; }
        Port src_port() const { return _src_port; }
        void src_port(Port p) { _src_port = p; }
        Port dst_port() const { return _dst_port; }
        void dst_port(Port p) { _dst_port = p; }
        PacketKind kind() const { return _kind; }
        void kind(PacketKind k) { _kind = k; }
    private:
        int64_t _timestamp; 
        Port _src_port; 
        Port _dst_port; 
        PacketKind _kind; 
    };

    static const unsigned int MTU = SharedMemoryNIC::MTU - sizeof(Header);
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

public:
    Protocol()
        : SharedMemoryNIC::Observer(),
        _shm_nic(),
        _socket_nic(nullptr),
        _address(_shm_nic.address(), 0),
        _is_master(false)
    {
        _shm_nic.attach(this, PROTO);

        if constexpr (!std::is_void_v<RawSocketNIC>) {
            if (SharedMemoryNIC::is_gateway_process()) {
                _socket_nic = new RawSocketNIC();
                _socket_nic->attach(this, PROTO);
            }
        }

        _instance = this;
    }

        ~Protocol() {
            disable_sync();
            _shm_nic.detach(this, PROTO);
            if (_socket_nic) {
                _socket_nic->detach(this, PROTO);
                delete _socket_nic;
            }
    }

    // pra comecar sync sptp. is_master define o papel: true = responde a
    // REQUEST_SYNC, false = envia REQUEST_SYNC e ajusta o relogio local
    void enable_sync(bool is_master) {
        if constexpr (!std::is_void_v<RawSocketNIC>) {
            if (!_socket_nic) {
                return;
            }
            if (_sptp) {
                return;
            }

            _is_master = is_master;
            // Etapa 4: a RSU e o unico no com is_master=true. Ela fixa o
            // quadrante no GPS -- e uma estacao fixa, nao se desloca.
            _socket_nic->set_fixed(is_master);
            Address own_addr(_socket_nic->address(), 0);
            _sptp = new SPTP_Protocol<Address>(own_addr, _is_master, &Protocol::send);
            _sptp->start();
        }
    }

    void disable_sync() {
        if constexpr (!std::is_void_v<RawSocketNIC>) {
            if (_sptp) {
                _sptp->stop();
                delete _sptp;
                _sptp = nullptr;
            }
        }
    }
    
    // tx_ts_out: se nao-nulo, recebe o now_ns() capturado o mais perto possivel
    // de nic->send(). usado pela sptp pra marcar t2 com simetria em relacao a t1
    // (que ja e gravado nesse mesmo ponto, dentro de send_via_nic).
    static int send(Address from, Address to, const void *data, unsigned int size, int64_t ts = 0, PacketKind kind = PacketKind::DATA, int64_t * tx_ts_out = nullptr) {
        if (!_instance) return -1;

        // decidimos que só os componentes de fato podem mandar mensagens, então isso aqui seria desnecessário
        // mas como ja estava feito deixe pra garantir, não adiciona custo em runtime devido a constexpr
        // a decisão de roteamento é feita no update do gateway
        if constexpr (!std::is_void_v<RawSocketNIC>){
            if (_instance->_socket_nic && !to.is_internal()) {
                return send_via_nic(_instance->_socket_nic, from, to, data, size, ts, kind, tx_ts_out);
            }
        }
        // se não usa shm
        return send_via_nic(&_instance->_shm_nic, from, to, data, size, ts, kind, tx_ts_out);

    };

    // ja é genérico o suficiente, vai servir caso algum dia o gatewaw queira ler mensagens 
    static int receive(Buffer *buf, Address *from, int64_t *ts, void *data, unsigned int size) {
        if (!_instance || !buf) return -1;
        if (buf->size() < sizeof(Header)) {
            free_buffer(buf);
            return -1;
        }

        Packet *packet = reinterpret_cast<Packet *>(buf->data()->payload());
        if (from) {
            *from = Address(buf->data()->src(), packet->src_port());
        }
        if (ts) {
            *ts = packet->timestamp();
        }

        unsigned int data_size = buf->size() - sizeof(Header);
        if (data_size > size) data_size = size;
        
        if (data && data_size)
            memcpy(data, packet->template data<unsigned char>(), data_size);


        // decide se é free no shm ou no raw socket
        free_buffer(buf);
        return static_cast<int>(data_size);
    }

    Address create_address(Port port){
        return Address(_address._paddr, port);
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
            free_buffer(buf);
            return;
        }

        Packet *packet = reinterpret_cast<Packet *>(buf->data()->payload());
        
        if constexpr (!std::is_void_v<RawSocketNIC>) {
            if (_socket_nic && _shm_nic.owns(buf)) {
                Physical_Address dst_mac = buf->data()->dst();
                Physical_Address src_mac = buf->data()->src();
                if (src_mac == _address._paddr && dst_mac == Ethernet::Address::BROADCAST) {
                    
                    // Não podemos chamar _socket_nic->send(buf) diretamente porque isso faria o
                    // _socket_nic tentar liberar um buffer que pertence ao _shm_nic, causando falha de segmentação.
                    // Também não devemos usar send_via_nic aqui porque ele adicionaria um novo cabeçalho do Protocolo,
                    // e nós queremos apenas repassar o frame Ethernet atual exatamente como está.
                    
                    Buffer* fwd_buf = _socket_nic->alloc(dst_mac, PROTO, buf->size());
                    if (fwd_buf) {
                        memcpy(fwd_buf->data()->payload(), buf->data()->payload(), buf->size());
                        _socket_nic->send(fwd_buf);
                    }
                }
            }

            // quando um pacote chega da rede:
            // se for sptp, consome aqui e nunca propaga pra SHM (independente
            // de quem deveria processar)
            // se for data, repassa pro SHM como antes
            if (_socket_nic && _socket_nic->owns(buf)) {
                PacketKind k = packet->kind();

                if (k == PacketKind::SPTP_SYNC || k == PacketKind::SPTP_REQUEST_SYNC) {
                    if (_sptp) {
                        Address src_addr(buf->data()->src(), packet->src_port());
                        unsigned int payload_size = (buf->size() > sizeof(Header))
                                                  ? buf->size() - sizeof(Header) : 0;
                        _sptp->on_receive(k, src_addr, packet->timestamp(),
                                          packet->template data<unsigned char>(), payload_size);
                    }
                    free_buffer(buf);
                    return;
                }

                Buffer *fwd_buf = _shm_nic.alloc(buf->data()->dst(), PROTO, buf->size());
                if (fwd_buf) {
                    std::memcpy(fwd_buf->data(), buf->data(), Ethernet::HEADER_SIZE + buf->size());
                    _shm_nic.send(fwd_buf);
                }

                free_buffer(buf);
                return;
            }
        }
        bool notified = _observed.notify(packet->dst_port(), buf);

        if (!notified)
            free_buffer(buf);
    }

    // o frame é o da ethernet independente da nic

    static void free_buffer(Buffer *buf) {
        if (!_instance) return;
        if (_instance->_shm_nic.owns(buf)) {
            _instance->_shm_nic.free(buf);
            return;
        }
        if constexpr (!std::is_void_v<RawSocketNIC>) {
            if (_instance->_socket_nic && _instance->_socket_nic->owns(buf)) {
                _instance->_socket_nic->free(buf);
            }
        }
    }

    // tx_ts_out: se nao-nulo, recebe o now_ns() usado pra marcar o packet header
    // garante simetria com a recepcao (que tambem usa now_ns no on_receive)
    template<typename NICType>
    static int send_via_nic(NICType *nic, Address from, Address to, const void *data, unsigned int size, int64_t ts, PacketKind kind, int64_t * tx_ts_out = nullptr) {
        Physical_Address physical_dst = to.paddr();

        Buffer *buf = nic->alloc(physical_dst, PROTO, sizeof(Header) + size);
        if (!buf) return -1;

        Packet *packet = reinterpret_cast<Packet *>(buf->data()->payload());
        packet->src_port(from.port());
        packet->dst_port(to.port());
        packet->kind(kind);

        if (data && size)
            memcpy(packet->template data<unsigned char>(), data, size);
        // TODO: monotonic_stamp sera?
        int64_t now = Clock::now_ns();
        packet->timestamp(ts != 0 ? ts : now);
        if (tx_ts_out) *tx_ts_out = now;
        return nic->send(buf);
    }


private:
    SharedMemoryNIC _shm_nic;
    RawSocketNIC *_socket_nic;
    Address _address;
    bool _is_master;
    // Channel protocols are usually singletons
    static Observed _observed;  
    static Protocol* _instance; // ponteiro pro singleton
    SPTP_Protocol<Address>* _sptp = nullptr;
};

// inicializacao dos static
template <typename S, typename R>
Protocol<S,R>* Protocol<S,R>::_instance = nullptr;

template <typename S, typename R>
typename Protocol<S,R>::Observed Protocol<S,R>::_observed;
#endif

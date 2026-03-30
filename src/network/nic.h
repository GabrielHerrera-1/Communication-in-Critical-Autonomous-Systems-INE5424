#ifndef NIC_H
#define NIC_H

#include "ethernet.h"
#include "../core/observers/conditional_data_observer.h"
#include "../core/buffer.h"
#include "../core/traits.h"
#include <pthread.h>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <stack>

// Network
template <typename Engine>
// só tem conditionally data observed declarado no observer.h da api do professor. no pdf ele usa conditional_data_observer. acho que é a mesma coisa
// NIC herda de ethernet, conditionally data observed e engine
class NIC : public Ethernet, public Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Ethernet::Protocol>, private Engine
{
public:
    static const unsigned int BUFFER_SIZE =
        Traits<NIC<Engine>>::SEND_BUFFERS +
        Traits<NIC<Engine>>::RECEIVE_BUFFERS;
    typedef Ethernet::Address Address;
    typedef Ethernet::Protocol Protocol_Number;
    typedef Conditional_Data_Observer<Buffer<Ethernet::Frame>, Ethernet::Protocol> Observer;
    typedef Conditionally_Data_Observed<Buffer<Ethernet::Frame>, Ethernet::Protocol> Observed;

    // professor usa mas não define (é um contador de métricas da nic). vai ser util pra debug
    struct Statistics {
        // tx_packets --> pacotes enviados
        // rx_packets --> pacotes recebidos
        // tx_bytes --> bytes enviados
        // rx_bytes --> bytes recebidos
        unsigned int tx_packets, rx_packets, tx_bytes, rx_bytes;
        // construtor da struct
        Statistics() : tx_packets(0), rx_packets(0), tx_bytes(0), rx_bytes(0) {} // lista de inicialização. seta tudo como zero
    };
    // construtor
    // botei NIC pra public porque nos testes a aplicação cria a NIC diretamente
    NIC() : _running(true) { // quando NIC criada inicializa running com true
        // inicia raw socket com a interface eth0
        Engine::engine_init(Traits<NIC<Engine>>::INTERFACE);

        // pergunta pro kernel qual o MAC da eth0 via chamada de sistema (ioctl)
        unsigned char mac[6];
        Engine::engine_get_address(mac);
        _address = Address(mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        // marcar todos buffers como livres
        for (unsigned int i = 0; i < BUFFER_SIZE; i++) {
            // cada elemento da stack contem um numero, esse numero representa o indice do buffer livre no array
            _free_list.push(i); // inicializa a free list com todos buffers livres
        }

        // thread de recepção sera iniciada no primeiro attach(), não aqui
        // assim receive() pode ser usado de forma segura enquanto ninguem se registrar como observer
    }

    // destrutor
    ~NIC() {
        _running = false;
        Engine::engine_close(); // fecha socket pra desbloquear recv, senão trava a thread
        if (_thread_started)
            pthread_join(_recv_thread, nullptr);
    };

    // versao de conveniencia, mesma coisa que o outro send, so muda os parametros
    int send(Address dst, Protocol_Number prot, const void *data, unsigned int size) {
        // aloca buffer
        Buffer<Ethernet::Frame> *buf = alloc(dst, prot, size);
        if (!buf) return -1;
        // bota os dados no buffer
        memcpy(buf->data()->payload(), data, size);
        return send(buf);
    };

    // recepcao direta sem observer. seguro de usar somente antes do primeiro attach()
    // depois que a thread de recepção inicia, o caminho correto é via observer: recv_loop -> notify -> Protocol::update
    int receive(Address *src, Protocol_Number *prot, void *data, unsigned int size) {
        Ethernet::Frame frame;
        int bytes = Engine::engine_receive(&frame, sizeof(frame));
        if (bytes <= 0) return bytes;
        if (bytes < static_cast<int>(Ethernet::HEADER_SIZE)) return -1;
        if (src) *src = frame.src();
        if (prot) *prot = frame.type();
        unsigned int payload_size = bytes - Ethernet::HEADER_SIZE;
        if (payload_size > size) payload_size = size;
        if (data && payload_size) memcpy(data, frame.payload(), payload_size);
        _statistics.rx_packets++;
        _statistics.rx_bytes += bytes;
        return payload_size;
    };

    // procura buffer livre no pool, trava ele e monta header ethernet
    Buffer<Ethernet::Frame> *alloc(Address dst, Protocol_Number prot, unsigned int size) {
        if (size > Ethernet::MTU) return nullptr;
        Buffer<Ethernet::Frame> *buf = alloc_buf();
        if (!buf) return nullptr;
        buf->size(size);
        Ethernet::Frame *f = buf->data();
        memcpy(f->dst().raw(), dst.raw(), 6);
        memcpy(f->src().raw(), _address.raw(), 6);
        f->type(prot);
        return buf;
    };

    // envia o frame montado pelo engine e libera o buffer
    int send(Buffer<Ethernet::Frame> *buf) {
        int bytes = Engine::engine_send(buf->data(), Ethernet::HEADER_SIZE + buf->size());
        if (bytes > 0) {
            _statistics.tx_packets++;
            _statistics.tx_bytes += bytes;
        }
        // libera buffer
        free(buf);
        return bytes;
    };

    // so devolve o buffer pro pool, marca como livre
    void free(Buffer<Ethernet::Frame> *buf) {
        if (!buf) return;

        // usa o mesmo mutex do alloc
        std::lock_guard<std::mutex> lock(_buf_mtx);
        // pega a posicao de buf dentro do array _buffer
        unsigned int idx = static_cast<unsigned int>(buf - _buffer);
        _free_list.push(idx);
    };

    // extrai os campos de um buffer recebido
    int unmarshal(Buffer<Ethernet::Frame> *buf, Address *src, Address *dst, void *data, unsigned int size) {
        Ethernet::Frame *f = buf->data();
        if (src) *src = f->src();
        if (dst) *dst = f->dst();
        unsigned int copy = buf->size();
        if (copy > size) copy = size;
        if (data && copy) memcpy(data, f->payload(), copy);
        return copy;
    };

    // getter
    const Address &address() {
        return _address;
    };

    // setter
    void address(Address address) {
        _address = address;
    };

    const Statistics &statistics() {
        return _statistics;
    };

    // inicia a thread de recepção no primeiro attach, antes disso receive() pode ser usado direto
    void attach(Observer *obs, Protocol_Number prot) {
        Observed::attach(obs, prot);
        // operacao atomica para ler comparar e escrever. evita concorrencia aqui no attach
        bool expected = false;
        if (_thread_started.compare_exchange_strong(expected, true)) {
            pthread_create(&_recv_thread, nullptr, recv_loop_entry, this);
        }
    };

    void detach(Observer *obs, Protocol_Number prot) {
        Observed::detach(obs, prot);
    }; // possibly inherited
private:
    // metodos privados

    // procura buffer livre no pool, trava e retorna. nullptr se cheio
    Buffer<Ethernet::Frame> *alloc_buf() {
        // lock guard destrava o mutex quando o objeto sai do escopo
        std::lock_guard<std::mutex> lock(_buf_mtx);

        if (_free_list.empty()) {
            return nullptr;
        }

        unsigned int idx = _free_list.top(); // pega o top da stack (o indice de um buffer livre)
        _free_list.pop();
        return &_buffer[idx];
    }

    // pega o ponteiro pra void e transforma de volta em ponteiro pra NIC, em seguida chama o metodo real
    static void *recv_loop_entry(void *arg) {
        reinterpret_cast<NIC *>(arg)->recv_loop();
        return nullptr;
    }

    void recv_loop() {
        while (_running) {
            // vamos receber diretamente no buffer, pra n precisar colocar o frame na stack antes
            Buffer<Ethernet::Frame> *buf = alloc_buf();

            int bytes = 0;

            if (buf) {
                // recebendo diretamente no buffer 
                bytes = Engine::engine_receive(buf->data(), sizeof(Ethernet::Frame));
            } else {
                // alloc falhou, pool cheio

                // frame temporario, so pra n deixar de consumir o pacote
                Ethernet::Frame temp;

                // so pra drenar o pacote do socket
                bytes = Engine::engine_receive(&temp, sizeof(Ethernet::Frame));
                if (bytes <= 0) {
                    break;
                }

                // como pool ta cheio, vai pra proxima iteração do loop
                continue;
            }

            if (bytes <= 0) {
                free(buf);
                break;
            }

            Ethernet::Frame *frame = buf->data();

            // descarta frames incompletos
            if (bytes < static_cast<int>(Ethernet::HEADER_SIZE)) {
                free(buf);
                continue;
            }

            // descarta o que a propria nic enviou
            if (frame->src() == _address) {
                free(buf);
                continue;
            }

            Protocol_Number prot = frame->type();

            _statistics.rx_packets++;
            _statistics.rx_bytes += bytes;

            buf->size(bytes - Ethernet::HEADER_SIZE);

            if (!Observed::notify(prot, buf)) {
                free(buf);
            }
        }
    }

    // attr privados
    Address _address;
    std::atomic<bool> _running;
    std::atomic<bool> _thread_started{false};
    pthread_t _recv_thread;
    std::mutex _buf_mtx;
    Statistics _statistics;
    Buffer<Ethernet::Frame> _buffer[BUFFER_SIZE];
    std::stack<unsigned int> _free_list; // usaremos free list para alocação dos buffers pois é O(1)
};

#endif

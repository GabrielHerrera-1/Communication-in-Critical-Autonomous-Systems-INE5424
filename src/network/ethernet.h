#ifndef ETHERNET_H
#define ETHERNET_H

#include <cstring> // para memset, memcpy e memcmp
#include <cstdint> // para uint8_t e uint16_t
#include <cstdio> // printf
#include <type_traits>

class Ethernet {
public:
    static const unsigned int MTU = 1500; // payload máximo de de um frame ethernet. pertence à classe (static)
    // destino (6) + origem (6) + tipo/protocolo (2) + quadrante (1).
    // Etapa 4: o quadrante espacial da origem viaja no header do frame para
    // que a NIC receptora descarte mensagens de outros quadrantes. Por estar
    // no header do frame, o forwarding shm<->raw socket (que ja faz memcpy de
    // HEADER_SIZE bytes) propaga o quadrante de graca.
    static const unsigned int HEADER_SIZE = 15;
    typedef uint16_t Protocol; // nome pro campo ethertype. 2 bytes (16 bits)

    class Address {
    public:
        static const unsigned int LENGTH = 6; // 6 bytes para o mac address

        // construtor default, todos bytes zerados (00:00:00:00:00:00)
        Address() { memset(_addr, 0, LENGTH); }

        // construtor com os 6 bytes explicitos. usado pra criar broadcast
        Address(uint8_t a, uint8_t b, uint8_t c,
                uint8_t d, uint8_t e, uint8_t f) {
                    _addr[0] = a;
                    _addr[1] = b;
                    _addr[2] = c;
                    _addr[3] = d;
                    _addr[4] = e;
                    _addr[5] = f;
                }
        // construtor que copia de um ponteiro. usado quando a chamada de sistema devolver o mac da interface. vem num unsigned char[6]
        Address(const uint8_t* raw) { memcpy(_addr, raw, LENGTH); }

        // declaracao do endereço broadcast
        static const Address BROADCAST; 
        // endereco sentinela usado para o barramento interno via SHM.
        // Mantemos isso centralizado aqui para que testes e pilha usem a
        // mesma convencao e nao espalhem "00:00:00:00:00:00" pelo codigo.
        static const Address INTERNAL;
        
        // definindo o que significa dois Address serem iguais. sobrecarga de operador
        bool operator == (const Address& o) const {
            return memcmp(_addr, o._addr, LENGTH) == 0;
        }

        // definindo o que significa ser diferente
        bool operator != (const Address& o) const {
            return !(*this == o);
        }

        // conversao para bool. retorna false se todos bytes forem zero
        operator bool() const {
            for (unsigned i = 0; i< LENGTH; i++) {
                if (_addr[i]) return true;
            }
            return false;
        }

        // só para leitura
        const uint8_t* raw() const {
            return _addr;
        }

        // leitura e escrita. usaremos quando o ioctl (chamada de sist pra ver o mac) retornar ele
        uint8_t* raw() {
            return _addr;
        }

        // imprime o mac no formato padrao
        void print() const {
            printf("%02x:%02x:%02x:%02x:%02x:%02x",
                   _addr[0],_addr[1],_addr[2],_addr[3],_addr[4],_addr[5]);
        }

    private:
        uint8_t _addr[LENGTH]; // dado real ta nesse array de 6 bytes privado
    };

    class Frame {
    public:
        // retorna referencia do destino. permite escrita. nic usara escrita no alloc
        Address& dst() { return _dst; }
        const Address& dst() const { return _dst; }
        // retorna referencia da origem. permite escrita
        Address& src() { return _src; }
        const Address& src() const { return _src; }
        // leitura do ethertype. monta o uint16_t a partir dos 2 bytes
        Protocol type() const { return (_type_hi << 8) | _type_lo; }
        // escrita do ethertype. fizemos assim para evitar bugs de endianness
        void type(Protocol p) { _type_hi = (p >> 8) & 0xFF; _type_lo = p & 0xFF; }
        // Etapa 4: quadrante espacial da origem (0..3, ou 0xFF se GPS ausente).
        // A NIC carimba no envio e a NIC receptora usa para decidir o drop.
        uint8_t quadrant() const { return _quadrant; }
        void quadrant(uint8_t q) { _quadrant = q; }
        // ponteiro pro inicio do payload. é void porque pode ser qualquer coisa no futuro
        void* payload() { return _payload; }
        // versao imutavel
        const void* payload() const { return _payload; }
        // calcula o tamanho total do frame dado o tamanho do payload. é static pq n depende de nenhuma instancia
        static unsigned int frame_size(unsigned int psz) { return HEADER_SIZE + psz; }

    private:
        Address _dst; // destino
        Address _src; // origem
        uint8_t _type_hi, _type_lo; // bytes mais e menos significativos do campo ethertype
        uint8_t _quadrant; // Etapa 4: quadrante espacial da origem
        uint8_t _payload[MTU]; // payload de ate 1500 bytes
    };

};

// pra garantir que os dados estão alinhados, impedir que o compilador adicione padding. o packet estava sendo ignorado
static_assert(sizeof(Ethernet::Address) == Ethernet::Address::LENGTH,
              "Ethernet::Address precisa ter exatamente 6 bytes");
static_assert(alignof(Ethernet::Address) == 1,
              "Ethernet::Address precisa ter alinhamento de 1 byte");
static_assert(std::is_standard_layout<Ethernet::Address>::value,
              "Ethernet::Address precisa ser standard-layout");
static_assert(sizeof(Ethernet::Frame) == Ethernet::HEADER_SIZE + Ethernet::MTU,
              "Ethernet::Frame precisa bater com o layout do frame Ethernet");
static_assert(alignof(Ethernet::Frame) == 1,
              "Ethernet::Frame precisa ter alinhamento de 1 byte");
static_assert(std::is_standard_layout<Ethernet::Frame>::value,
              "Ethernet::Frame precisa ser standard-layout");

#endif

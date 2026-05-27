#ifndef MESSAGE_H
#define MESSAGE_H
	
#include <cstring>
#include <cstdint>
#include "../application/vehicle_protocol.h"

class Message {
public:
    class Origin
    {
    public:
        Origin(){}

        ~Origin(){}

        const Vehicle_Protocol::Port port() const { return address.port(); } 

        const Vehicle_Protocol::Physical_Address physical_address() const { return address.paddr(); }

        Vehicle_Protocol::Address address;
        uint8_t quadrant;   

    };
    

    // acessa os campos privados pra escreve nos campos readonly do usuário
    template <typename Channel>
    friend class Communicator;

    static const unsigned int MAX_SIZE = 1400;
    // Etapa 4: quadrante "desconhecido" (GPS ausente / mensagem intra-veiculo)
    static const uint8_t QUADRANT_NONE = 0xFF;

    Message() : _origin(), _timestamp(0), _size(MAX_SIZE) {
        memset(_payload, 0, sizeof(_payload));
    }

    Message(const void* data, unsigned int size)
        : _origin(), _timestamp(0), _size(0) {
        memset(_payload, 0, sizeof(_payload));
        if (data && size > 0) {
            _size = (size <= MAX_SIZE) ? size : MAX_SIZE;
            memcpy(_payload, data, _size);
        }
    }

    //Métodos exigidos pelo Communicator

    const void* data() const {
        return _payload;
    }

    void* data() {
        return _payload;
    }

    unsigned int size() const {
        return _size;
    }

    void size(unsigned int s) {
        _size = (s <= MAX_SIZE) ? s : MAX_SIZE;
    }

    const Origin & origin() const {
        return _origin;
    }

    int64_t timestamp() const {
        return _timestamp;
    }
    
    // Etapa 4: quadrante espacial da origem, etiquetado na mensagem.
    // QUADRANT_NONE quando a mensagem e intra-veiculo (SHM) ou quando o GPS
    // nao estava disponivel na origem.
    uint8_t quadrant() const {
        return _origin.quadrant;
    }

private:
    Origin _origin;
    int64_t _timestamp;
    unsigned char _payload[MAX_SIZE];
    unsigned int _size;
};

#endif
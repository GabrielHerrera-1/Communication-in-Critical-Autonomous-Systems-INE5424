#ifndef BUFFER_H
#define BUFFER_H

// template porque a api especificada pelo professor usa Buffer<Ethernet::Frame>
template<typename T> class Buffer {
    public:

    // frame real fica aqui dentro. NIC aloca um buffer, preenche o frame e manda pro engine
    T* data() {
        return &_data;
    }
    // versao somente pra leitura
    const T* data() const {
        return &_data;
    }

    // tamanho do payload dentro do frame
    unsigned int size() const {
        return _size;
    }
    // NIC seta quando aloca, protocol le quando recebe
    void size(unsigned int s) {
        _size = s;
    }

    private:
    // frame ethernet em si
    T _data;
    // quantos bytes de payload dentro do frame estão sendo usados de fato (o maximo que definimos antes é 1500)
    unsigned int _size{0};
};

#endif

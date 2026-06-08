#ifndef IPRODUCER_H
#define IPRODUCER_H

// interface de produtor de dados, implementada pelos COMPONENTES
template <typename Value>
class IProducer {
public:
    virtual ~IProducer() {}
    virtual Value produce() = 0;
};

#endif

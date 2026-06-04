#ifndef IPRODUCER_H
#define IPRODUCER_H

// Interface de produtor de dados, implementada pelos COMPONENTES.
//
// A producao/aquisicao do dado vive no componente (que sabe ler seu sensor,
// integrar um valor, etc.), nao numa classe escondida. O SmartData responsivo
// recebe um IProducer e chama produce() para gerar cada Resposta.
template <typename Value>
class IProducer {
public:
    virtual ~IProducer() {}
    virtual Value produce() = 0;
};

#endif

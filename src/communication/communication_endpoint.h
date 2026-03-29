#ifndef COMMUNICATION_ENDPOINT_H
#define COMMUNICATION_ENDPOINT_H

#include "message.h"

// Interface comum para comunicacao de alto nivel.
// Os componentes dependem apenas desta API; a implementacao concreta
// pode ser baseada em rede na etapa 1 e em IPC na etapa 2.
class Communication_Endpoint {
public:
    virtual ~Communication_Endpoint() {}

    virtual bool send(const Message * message) = 0;
    virtual bool receive(Message * message) = 0;
};

#endif

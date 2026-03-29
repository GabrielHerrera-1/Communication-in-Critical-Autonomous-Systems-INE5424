#ifndef COMPONENT_H
#define COMPONENT_H

#include <cstdint>
#include <string>

#include "../../communication/communication_endpoint.h"

// classe base abstrata para qualquer componente do veiculo (sensor ou atuador)
// cada componente roda como processo separado via fork()
class Component {
public:
    typedef uint16_t Port;

    Component(const std::string& id);
    virtual ~Component();

    // prepara o componente antes de rodar (calibração, etc)
    virtual void initialize() = 0;
    // loop principal do componente, executado no processo filho
    virtual void run() = 0;

    const std::string& id() const;

    // O endpoint concreto eh injetado pelo ambiente de execucao.
    // Assim o componente nao conhece Vehicle_Protocol nem RawSocketEngine.
    void set_endpoint(Communication_Endpoint * endpoint);
    void set_port(Port port);

protected:
    std::string _id;
    Port _port;
    Communication_Endpoint * _endpoint;
    
};

#endif

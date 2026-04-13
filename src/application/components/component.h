#ifndef COMPONENT_H
#define COMPONENT_H

#include <cstdint>
#include <string>

#include "../component_ports.h"
#include "../vehicle_protocol.h"
#include "../../communication/communicator.h"

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
    // porta logica fixa usada para identificar o tipo do componente
    virtual Port logical_port() const = 0;
    // Por padrao componentes escutam o grupo de broadcast logico. Senders puros
    // podem sobrescrever isso para evitar acumular mensagens que nunca serao lidas.
    virtual bool subscribe_logical_broadcast() const { return true; }

    const std::string& id() const;

    void set_communicator(Communicator<Vehicle_Protocol> * communicator);
    void set_port(Port port);

protected:
    std::string _id;
    Port _port;
    Communicator<Vehicle_Protocol> * _communicator;
    
};

#endif

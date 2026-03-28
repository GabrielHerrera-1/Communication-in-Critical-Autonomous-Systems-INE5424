#ifndef COMPONENT_H
#define COMPONENT_H

#include <string>

#include "../../communication/communicator.h"
#include "../vehicle_protocol.h"

// classe base abstrata para qualquer componente do veiculo (sensor ou atuador)
// cada componente roda como processo separado via fork()

// no futuro um componente deve ser um template, onde é passado o tpo do comunicator que ele tem
class Component {
public:
    Component(const std::string& id);
    virtual ~Component();

    // prepara o componente antes de rodar (calibração, etc)
    virtual void initialize() = 0;
    // loop principal do componente, executado no processo filho
    virtual void run() = 0;

    const std::string& id() const;

    // CComunicator::Protocol::Port
    void set_comunicator(Communicator<Vehicle_Protocol>* Communicator);

protected:
    std::string _id;
    
    Communicator<Vehicle_Protocol>* _comnunicator;
    
};

#endif

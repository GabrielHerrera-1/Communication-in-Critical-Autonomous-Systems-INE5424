#ifndef COMPONENT_H
#define COMPONENT_H

#include <string>

// classe base abstrata para qualquer componente do veiculo (sensor ou atuador)
// cada componente roda como processo separado via fork()
class Component {
public:
    Component(const std::string& id);
    virtual ~Component();

    // prepara o componente antes de rodar (calibração, etc)
    virtual void initialize() = 0;
    // loop principal do componente, executado no processo filho
    virtual void run() = 0;

    const std::string& id() const;

protected:
    std::string _id;
};

#endif

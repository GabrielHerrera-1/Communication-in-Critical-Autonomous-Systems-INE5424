#ifndef ACTUATOR_H
#define ACTUATOR_H

#include "../component.h"

// atuadores aplicam comandos sobre o veiculo (acelerar, trocar marcha, etc)
class Actuator : public Component {
public:
    Actuator(const std::string& id);
    virtual ~Actuator();

    // aplica um valor de comando ao atuador
    virtual void apply(double value) = 0;
};

#endif

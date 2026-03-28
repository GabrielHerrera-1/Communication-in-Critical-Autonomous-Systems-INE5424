#include "component.h"

// o c++ não deixa o construtor deixar _adddress e _comunicator não inicializados

Component::Component(const std::string& id)
    : _id(id),
    _comnunicator{}
{
    
}

Component::~Component() {
    delete _comnunicator;
}

const std::string& Component::id() const {
    return _id;
}

void Component::set_comunicator(Communicator<Vehicle_Protocol>* comunicator){
    _comnunicator = comunicator;
}

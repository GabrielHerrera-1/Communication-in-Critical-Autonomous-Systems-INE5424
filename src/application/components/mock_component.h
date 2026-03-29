#ifndef MOCK_COMPONENT
#define MOCK_COMPONENT

#include "component.h"

class Mock_Component: public Component{
public:

    Mock_Component(const std::string& id);

    void initialize() override; 

    void run() override;

private:

    void send_all_messages();

    void receive_all_messages();

};

#endif
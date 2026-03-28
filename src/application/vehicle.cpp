#include "vehicle.h"
#include <sys/wait.h>
#include <iostream>
#include <thread>

typedef Vehicle_Protocol::Address Vehicle_Address;

Vehicle::Vehicle(Port port)
        : _nic()
        , _protocol(&_nic)
        , _addr(_nic.address(),port)    
        , _communicator(&_protocol, _addr)
        {}

Vehicle::~Vehicle() = default;

Vehicle_Address Vehicle::you_are(){

        Message m;
        _communicator.receive(&m);
        Vehicle_Address* addr = reinterpret_cast<Vehicle_Address*>(m.data());

        if (addr){
                std::cout << "Received address hex: " << *addr << std::endl;
        }
        
        return *addr;
                
}

void Vehicle::i_am_loop(){

        const Message iam((void*) &_addr, sizeof(_addr));
        for (int i = 0; i < 10; i++){
                sleep(2);
                _communicator.send(&iam);
        }


}

void Vehicle::init(){

        std::thread t1(&Vehicle::i_am_loop, this);

        Vehicle_Address found = you_are();

        if(t1.joinable()) {
                t1.join();
        }
        
}

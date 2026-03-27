    #ifndef VEHICLE
    #define VEHICLE

    #include "../nic.h"
    #include "../ethernet.h"
    #include "../engine/raw_socket_engine.h"
    #include "../protocol.h"
    #include "../comunicator.h"
    #include "vehicle_protocol.h"

    class Vehicle {
    public:

        typedef Vehicle_Protocol::Port Port;
        typedef Vehicle_Protocol::Address Vehicle_Address;

        // talvez ?
        Vehicle(Port port);
        ~Vehicle();

        void init();

    private:
        
        NIC<RawSocketEngine> _nic;
        Vehicle_Protocol _protocol;
        Vehicle_Address _addr;
        Communicator<Vehicle_Protocol> _communicator;
        
        void i_am_loop();

        Vehicle_Address you_are();

        std::mutex _communicator_mutex;
    };

    #endif
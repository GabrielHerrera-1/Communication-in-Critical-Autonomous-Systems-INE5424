    #ifndef VEHICLE
    #define VEHICLE

    #include "../network/nic.h"
    #include "../network/ethernet.h"
    #include "../network/engine/raw_socket_engine.h"
    #include "../channel/protocol.h"
    #include "../communication/communicator.h"
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
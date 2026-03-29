#include "mock_component.h"

#include <iostream>
#include <vector>
#include <sys/wait.h>
#include <thread>

inline void build_payload(
    char * buffer,
    std::size_t size,
    int round
) {
    std::snprintf(
        buffer,
        size,
        "VM:r%d",
        round
    );
}

Mock_Component::Mock_Component(const std::string& id):
    Component(id){}


void Mock_Component::initialize(){
    // does nothing in this case
} 

void Mock_Component::run(){

    sleep(3);

    std::thread receiver(&Mock_Component::receive_all_messages, this);

    sleep(3);

    send_all_messages();
    
    receiver.join();
}

void Mock_Component::send_all_messages() {
    for (int round = 0; round < 5; ++round) {
        char payload[96];
        build_payload(payload, sizeof(payload), round);

        Message message(payload, std::strlen(payload) + 1);
        if (!_endpoint || !_endpoint->send(&message)) {
            std::cerr << "[" << _id << "]" <<" falha ao enviar " << payload << std::endl;
            std::exit(1);
        }

                
        std::cout << "[" << _id << "]" << " enviou "<< payload << std::endl;
        
        sleep(5);
    }
}

void Mock_Component::receive_all_messages(){
    std::cout << "[" << _id << "]" << " escutando a rede " << std::endl;
    const int total_vehicles = 2;   // only two vehicles, initially
    const int expected_total = (total_vehicles - 1) * 5; // 5 rounds

    int receive_count = 0;

    while (receive_count < expected_total) {
        std::cout << "[" << _id << "]" << " pronto para receber o primeiro frame " << std::endl;
        Message message;
        if (!_endpoint || !_endpoint->receive(&message)) {
            std::cerr << "[" << _id << "]" <<" falha ao receber " << std::endl;
            std::exit(1);
        }

        const char * payload = reinterpret_cast<const char *>(message.data());

        char parsed_label[64];
        int r = 0;

        if (std::sscanf(payload, "%63[^:]:r%d", parsed_label, &r) != 2) {
            std::cerr << "[" << _id << "] Payload malformado: " << payload << std::endl;
            continue; 
        }

        std::cout << "Recebido do label " << parsed_label << " no round " << r << std::endl;

        receive_count++;
    }
}

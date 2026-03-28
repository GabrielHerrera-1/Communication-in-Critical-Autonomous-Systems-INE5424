#include "vehicle.h"
#include <sys/wait.h>
#include <iostream>
#include <thread>
#include <unistd.h>

Vehicle::Vehicle(Port port)
    : _nic()
    , _protocol(&_nic)
    , _addr(_nic.address(), port)
    , _communicator(&_protocol, _addr)
    {}

Vehicle::~Vehicle() {
    for (auto c : _components)
        delete c;
}

void Vehicle::add_component(Component* component) {
    _components.push_back(component);
}

void Vehicle::initialize() {
    for (auto c : _components)
        c->initialize();
}

void Vehicle::run() {
    std::vector<pid_t> pids;

    for (auto c : _components) {
        pid_t pid = fork();
        if (pid == 0) {
            c->run();
            _exit(0);
        } else if (pid > 0) {
            pids.push_back(pid);
        } else {
            std::cerr << "[Vehicle] fork falhou para " << c->id() << std::endl;
        }
    }

    std::thread sender(&Vehicle::i_am_loop, this);

    Address peer = you_are();

    if (sender.joinable())
        sender.join();

    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);
    }

    std::cout << "[Vehicle] encerrado." << std::endl;
}

void Vehicle::i_am_loop() {
    sleep(1); 
    const Message iam(&_addr, sizeof(_addr));
    for (int i = 0; i < 10; i++) {
        _communicator.send(&iam);
        std::cout << "[Vehicle] broadcast i_am #" << (i + 1) << std::endl;
        sleep(2);
    }
}

Vehicle::Address Vehicle::you_are() {
    Message m;
    _communicator.receive(&m);
    Address* addr = reinterpret_cast<Address*>(m.data());
    if (addr)
        std::cout << "[Vehicle] peer encontrado" << std::endl;
    return *addr;
}
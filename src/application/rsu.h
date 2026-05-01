#ifndef RSU_H
#define RSU_H

#include "gateway.h"


class RSU {
public:
    RSU();
    ~RSU();

    void initialize();
    void run();

private:
    int run_gateway_process();

    Gateway _gateway;
};

#endif

#ifndef INTEREST_TEST_UTILS_H
#define INTEREST_TEST_UTILS_H

#include "../src/channel/vehicle_protocol.h"
#include "../src/communication/message/message.h"
#include "../src/communication/smart_data/smart_message.h"
#include "../src/communication/smart_data/unit.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <vector>

inline int detect_vm_id(const char * label, int max_vm) {
    FILE * cmdline = std::fopen("/proc/cmdline", "r");
    if (!cmdline) {
        std::cerr << "[" << label << "] sem /proc/cmdline" << std::endl;
        std::exit(1);
    }

    char line[4096];
    if (!std::fgets(line, sizeof(line), cmdline)) {
        std::fclose(cmdline);
        std::cerr << "[" << label << "] falha lendo /proc/cmdline" << std::endl;
        std::exit(1);
    }
    std::fclose(cmdline);

    for (char * tok = std::strtok(line, " "); tok; tok = std::strtok(nullptr, " ")) {
        int vm_id = 0;
        if (std::sscanf(tok, "so2.vm_id=%d", &vm_id) == 1) {
            if (vm_id < 1 || vm_id > max_vm) {
                std::cerr << "[" << label << "] vm_id invalido: " << vm_id << std::endl;
                std::exit(1);
            }
            return vm_id;
        }
    }

    std::cerr << "[" << label << "] so2.vm_id ausente" << std::endl;
    std::exit(1);
}

inline uint64_t endpoint_key(const Vehicle_Protocol::Address & address) {
    const uint8_t * mac = address.paddr().raw();
    uint64_t key = 0;
    for (int i = 0; i < 6; ++i) key = (key << 8) | mac[i];
    return (key << 16) | address.port();
}

inline const SmartHeader * smart_header(Message * message) {
    if (!message || message->size() < sizeof(SmartHeader)) return nullptr;
    return reinterpret_cast<const SmartHeader *>(message->data());
}

template <typename Data>
bool decode_response(Message * message, typename Data::Value * value = nullptr) {
    const SmartHeader * header = smart_header(message);
    if (!header) return false;
    if (header->kind != SmartHeader::RESPONSE || header->unit != Data::UNIT) return false;
    if (message->size() < sizeof(ResponseMessage<typename Data::Value>)) return false;
    if (value) {
        *value = reinterpret_cast<const ResponseMessage<typename Data::Value> *>(
            message->data())->value;
    }
    return true;
}

inline int64_t average_after_warmup(std::vector<int64_t> values, std::size_t warmup) {
    if (values.size() <= warmup) return 0;
    values.erase(values.begin(), values.begin() + static_cast<long>(warmup));
    int64_t sum = 0;
    for (int64_t v : values) sum += v;
    return sum / static_cast<int64_t>(values.size());
}

#endif

#pragma once
#include <stdint.h>

namespace Cluster {
    struct Node {
        uint32_t id;
        uint32_t ip_address;
        uint16_t port;
        bool active;
        char name[32];
    };

    bool init();
    int add_node(uint32_t ip, uint16_t port, const char* name);
    bool remove_node(uint32_t id);
    uint32_t get_count();
    uint32_t get_local_id();
    const Node* get_node(uint32_t idx);
    static constexpr uint32_t MAX_NODES = 16;
}

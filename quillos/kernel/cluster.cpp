#include "cluster.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Cluster {

    static Node nodes[MAX_NODES];
    static uint32_t node_count = 0;
    static uint32_t local_node_id = 0;
    static bool initialized = false;

    static void str_copy(char* dst, const char* src) {
        int i = 0;
        while (src[i] && i < 31) { dst[i] = src[i]; i++; }
        dst[i] = '\0';
    }

    bool init() {
        for (uint32_t i = 0; i < MAX_NODES; i++) {
            nodes[i].active = false;
            nodes[i].id = 0;
            nodes[i].ip_address = 0;
            nodes[i].port = 0;
            for (int j = 0; j < 32; j++) nodes[i].name[j] = 0;
        }

        // Register self as node 0
        nodes[0].id = 0;
        nodes[0].ip_address = 0x0A000002; // 10.0.0.2
        nodes[0].port = 9000;
        nodes[0].active = true;
        str_copy(nodes[0].name, "local");

        node_count = 1;
        local_node_id = 0;
        initialized = true;

        console_print("\n[CLUSTER] Node 0 (local) registered at 10.0.0.2:9000");
        return true;
    }

    int add_node(uint32_t ip, uint16_t port, const char* name) {
        if (!initialized) return -1;

        for (uint32_t i = 1; i < MAX_NODES; i++) {
            if (!nodes[i].active) {
                nodes[i].id = i;
                nodes[i].ip_address = ip;
                nodes[i].port = port;
                nodes[i].active = true;
                str_copy(nodes[i].name, name ? name : "node");

                node_count++;

                char buf[16];
                console_print("\n[CLUSTER] Added node ");
                itoa(i, buf);
                console_print(buf);
                console_print(": ");
                console_print(nodes[i].name);

                return (int)i;
            }
        }
        return -1;
    }

    bool remove_node(uint32_t id) {
        if (id == 0 || id >= MAX_NODES || !nodes[id].active) return false;
        nodes[id].active = false;
        node_count--;
        return true;
    }

    uint32_t get_count() { return node_count; }
    uint32_t get_local_id() { return local_node_id; }

    const Node* get_node(uint32_t idx) {
        if (idx >= MAX_NODES || !nodes[idx].active) return nullptr;
        return &nodes[idx];
    }
}

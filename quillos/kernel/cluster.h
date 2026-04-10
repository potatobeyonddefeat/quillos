#pragma once
#include <stdint.h>

namespace Cluster {

    struct Node {
        uint32_t ip;
        uint8_t  mac[6];
        char     name[16];
        uint64_t last_seen;  // Tick count when last heard from
        bool     active;
    };

    struct JobResult {
        uint32_t job_id;
        bool     completed;
        uint32_t result;
    };

    static constexpr uint32_t MAX_NODES = 8;
    static constexpr uint16_t CLUSTER_PORT = 9000;

    bool init();

    // Discovery
    void send_discover();
    uint32_t get_peer_count();
    const Node* get_peer(uint32_t idx);

    // Job dispatch
    bool submit_job(uint32_t target_ip, uint8_t job_type, const uint8_t* data, uint16_t len);
    const JobResult* get_last_result();

    // Background tasks (run by scheduler)
    void poll_task_entry();
    void heartbeat_task_entry();

    // Local node info
    uint32_t get_local_ip();
    const char* get_local_name();
}

#pragma once
#include <stdint.h>
#include "jobs.h"

// ================================================================
// Distributed Job Scheduler
//
// Decides whether to run a job locally or send it to a peer.
// Decision is based on load: task count on each node.
// Tracks job history for status queries.
// ================================================================

namespace DJob {

    static constexpr uint32_t MAX_HISTORY = 16;

    // Node load info (exchanged via cluster protocol)
    struct NodeLoad {
        uint32_t ip;
        uint32_t task_count;      // Number of active scheduler tasks
        uint64_t last_updated;    // Tick when this was received
        bool     valid;
    };

    bool init();

    // Submit a job — scheduler decides where to run it.
    // Returns the job ID.
    uint32_t submit(Jobs::Type type, const uint8_t* data, uint16_t len);

    // Query status of a submitted job
    const Jobs::Job* get_job(uint32_t id);

    // Get the most recent completed job
    const Jobs::Job* get_last_completed();

    // Get job history for shell display
    uint32_t get_history_count();
    const Jobs::Job* get_history(uint32_t idx);

    // Update a remote node's load (called by cluster protocol)
    void update_node_load(uint32_t ip, uint32_t task_count);

    // Handle a remote job result arriving
    void on_remote_result(uint32_t job_id, uint32_t result);

    // Get local load metric
    uint32_t get_local_load();
}

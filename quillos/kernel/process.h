#pragma once
#include <stdint.h>

// ================================================================
// Distributed Process Manager
//
// Processes are long-running tasks that can span the cluster.
// Unlike jobs (fire-and-forget), processes have a lifecycle:
// spawn → running → (optional sleep) → exiting → dead
//
// Each process maps to a local scheduler task on whichever
// node it's running on. The process table tracks ALL processes
// across the cluster (local + remote).
// ================================================================

namespace Process {

    enum State : uint8_t {
        PROC_UNUSED  = 0,
        PROC_RUNNING = 1,
        PROC_SLEEPING = 2,
        PROC_EXITING = 3,
        PROC_DEAD    = 4,
    };

    enum Type : uint8_t {
        TYPE_COUNTER  = 1,  // Prints a counter every second
        TYPE_STRESS   = 2,  // CPU stress loop
        TYPE_MONITOR  = 3,  // Periodic system stats
        TYPE_WORKER   = 4,  // Idle worker waiting for jobs
    };

    struct Info {
        uint32_t pid;
        Type     type;
        State    state;
        uint32_t node_ip;      // Node where it's actually running (0 = local)
        uint32_t owner_ip;     // Node that requested the spawn
        char     name[24];
        uint64_t started_at;
        uint64_t cpu_ticks;
        int32_t  sched_slot;   // Local scheduler task slot (-1 if remote)
    };

    static constexpr uint32_t MAX_PROCS = 16;

    bool init();

    // Spawn — auto-picks node via load balancer
    uint32_t spawn(const char* type_name);

    // Spawn on a specific node (0 = local)
    uint32_t spawn_on(const char* type_name, uint32_t node_ip);

    // Kill by PID (works for local and remote)
    bool kill(uint32_t pid);

    // Queries
    uint32_t count();
    const Info* get_by_index(uint32_t idx);
    const Info* find_pid(uint32_t pid);

    // Type helpers
    Type parse_type(const char* name);
    const char* type_name(Type t);

    // Called by cluster protocol for remote process events
    void on_remote_spawn_request(uint32_t from_ip, Type type, uint32_t remote_pid);
    void on_remote_spawned(uint32_t pid, uint32_t node_ip);
    void on_remote_kill_request(uint32_t pid);
    void on_remote_died(uint32_t pid, uint32_t node_ip);
    void on_remote_status(uint32_t pid, uint32_t node_ip, State state, uint64_t cpu);

    // Background: reports local process status to peers
    void status_report_entry();
}

#pragma once
#include <stdint.h>

struct InterruptFrame;

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
//
// User processes (TYPE_USER) run in ring 3 with their own
// address space and talk to the kernel via int 0x80 syscalls.
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
        TYPE_USER     = 5,  // Ring-3 user process (ELF or embedded blob)
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
        uint64_t cr3;          // User address space (0 for kernel procs)
        int32_t  exit_status;  // Set when state becomes PROC_DEAD
    };

    static constexpr uint32_t MAX_PROCS = 16;

    bool init();

    // Spawn — auto-picks node via load balancer
    uint32_t spawn(const char* type_name);

    // Spawn on a specific node (0 = local)
    uint32_t spawn_on(const char* type_name, uint32_t node_ip);

    // Load + run a user program from an in-memory ELF64 image.
    // Returns the new pid or 0 on failure. Runs locally.
    uint32_t spawn_user_elf(const char* name,
                            const void* elf_bytes,
                            uint64_t elf_len);

    // Load + run a user program from a raw machine-code blob.
    // The blob is loaded at `load_addr` (defaults to 0x400000)
    // and execution starts at that same address. Useful for
    // tiny hand-assembled test programs without an ELF header.
    uint32_t spawn_user_blob(const char* name,
                             const void* code,
                             uint64_t code_len,
                             uint64_t load_addr);

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

    // ----------------------------------------------------------
    // Syscall entry points (called from Syscall::dispatch)
    //
    // Return new RSP where useful (sleep/exit), otherwise return
    // a scalar result and let the caller stuff it into frame->rax.
    // ----------------------------------------------------------
    uint64_t sys_write(int fd, const char* buf, uint64_t len);
    uint64_t sys_read(int fd, char* buf, uint64_t len);
    int32_t  sys_getpid();
    uint64_t sys_exit(int32_t status, InterruptFrame* frame);
    uint64_t sys_sleep_ms(uint32_t ms, InterruptFrame* frame);
    int32_t  sys_fork(InterruptFrame* parent_frame);
    int32_t  sys_exec(const char* path, InterruptFrame* frame);

    // Lookup helper: which proc slot owns the currently-running
    // scheduler task? Returns -1 if none.
    int current_slot();
}

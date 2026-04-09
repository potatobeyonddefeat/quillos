#pragma once
#include <stdint.h>

struct InterruptFrame;

namespace Scheduler {

    enum TaskState : uint8_t {
        TASK_UNUSED = 0,
        TASK_READY,
        TASK_RUNNING,
        TASK_SLEEPING,
        TASK_DEAD,
    };

    struct Task {
        uint64_t rsp;
        TaskState state;
        char name[32];
        uint32_t id;
        uint64_t ticks_used;
        uint64_t wake_at;
    };

    static constexpr uint32_t MAX_TASKS = 16;
    static constexpr uint32_t TIME_SLICE_MS = 10;  // 10ms per slice at 1000Hz

    bool init();

    // Create a new kernel task. Returns task ID or -1.
    int create_task(const char* name, void (*entry)());

    // Kill a task by ID (cannot kill task 0 or idle)
    bool kill_task(uint32_t id);

    // Called by IRQ 0 dispatch — returns RSP to pop from (may differ if switched)
    uint64_t timer_tick(uint64_t current_rsp);

    // Called by INT 0x80 dispatch — voluntary yield, returns new RSP
    uint64_t yield(uint64_t current_rsp);

    // Sleep the current task for N milliseconds, then yield
    void sleep_ms(uint32_t ms);

    // Info for shell commands
    uint32_t get_count();
    uint32_t get_current();
    const Task* get_task(uint32_t idx);
}

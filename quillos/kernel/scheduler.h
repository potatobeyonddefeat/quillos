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
        bool is_user;
        uint64_t cr3;           // 0 = use kernel CR3
    };

    static constexpr uint32_t MAX_TASKS = 16;
    static constexpr uint32_t TIME_SLICE_MS = 10;  // 10ms per slice at 1000Hz

    bool init();

    // Create a new kernel task. Returns task slot or -1.
    int create_task(const char* name, void (*entry)());

    // Create a user task.
    //   cr3              = the new address space (from VMM::create_address_space)
    //   user_entry       = user-mode RIP
    //   user_stack_top   = user-mode RSP
    // Returns task slot or -1.
    int create_user_task(const char* name,
                         uint64_t cr3,
                         uint64_t user_entry,
                         uint64_t user_stack_top);

    // Clone an existing user task (used by fork()). Copies the
    // parent's kernel-stack interrupt frame onto the child's
    // fresh kernel stack, then sets the child's return value to
    // 0. The child shares `cr3` with whoever calls (normally the
    // caller passes a freshly-cloned address space).
    int fork_user_task(const char* name,
                       uint64_t cr3,
                       const InterruptFrame* parent_frame);

    // Kill a task by slot (cannot kill task 0 or idle)
    bool kill_task(uint32_t id);

    // Called by IRQ 0 dispatch — returns RSP to pop from (may differ if switched)
    uint64_t timer_tick(uint64_t current_rsp);

    // Called by INT 0x80 dispatch — voluntary yield, returns new RSP
    uint64_t yield(uint64_t current_rsp);

    // Sleep the current task for N milliseconds, then yield
    void sleep_ms(uint32_t ms);

    // Sleep the current task for N milliseconds, but treat
    // `frame_rsp` as the saved context (called from a syscall
    // context). Returns the RSP to iretq to.
    uint64_t sleep_ms_from_frame(uint32_t ms, uint64_t frame_rsp);

    // Mark the current task dead. Returns the RSP to iretq to,
    // which will be a different (live) task. Never returns to
    // the caller's task.
    uint64_t exit_current(uint64_t frame_rsp);

    // Apply the current task's CR3 + TSS.rsp0. Called from the
    // dispatcher after any potential context switch.
    void apply_task_context();

    // Info for shell commands
    uint32_t get_count();
    uint32_t get_current();
    const Task* get_task(uint32_t idx);

    // Return the (virtual) kernel-stack top for a task slot. Used
    // by TSS.rsp0 when the task is in user mode.
    uint64_t kstack_top(uint32_t slot);

    // Look up the pid-side owner of a given task slot (set by
    // process manager via set_slot_pid). Used by syscalls that
    // need to find the current process.
    void set_slot_pid(uint32_t slot, uint32_t pid);
    uint32_t get_slot_pid(uint32_t slot);

    // Return the InterruptFrame* saved at a task's current rsp.
    // Only valid when that task is NOT currently running.
    InterruptFrame* saved_frame(uint32_t slot);

    // Change the CR3 of the currently-running task in place.
    // Used by exec().
    void retarget_current_cr3(uint64_t new_cr3);
}

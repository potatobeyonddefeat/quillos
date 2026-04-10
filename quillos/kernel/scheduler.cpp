#include "scheduler.h"
#include "idt.h"
#include <stdint.h>
#include <stddef.h>

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);
extern volatile uint64_t ticks;

namespace Scheduler {

    static constexpr uint64_t STACK_SIZE = 16384;  // 16KB per task

    // Per-task kernel stacks (statically allocated)
    static uint8_t task_stacks[MAX_TASKS][STACK_SIZE]
        __attribute__((aligned(16)));

    struct TaskInternal {
        uint64_t rsp;
        TaskState state;
        char name[32];
        uint32_t id;
        void (*entry)();
        uint64_t ticks_used;
        uint64_t wake_at;
        uint32_t slice_remaining;
    };

    static TaskInternal tasks[MAX_TASKS];
    static uint32_t current_task = 0;
    static uint32_t task_count = 0;
    static uint32_t idle_task_id = 0;
    static uint32_t next_id = 0;
    static bool initialized = false;

    // ================================================================
    // Idle task — runs when no other task is ready
    // ================================================================
    static void idle_entry() {
        for (;;) {
            asm volatile("sti; hlt");
        }
    }

    // ================================================================
    // Task entry wrapper — runs the task function, cleans up on return
    // ================================================================
    static void task_entry_wrapper() {
        // Enable interrupts (we were switched in via iretq with IF=1,
        // but be safe)
        asm volatile("sti");

        uint32_t my_id = current_task;
        if (tasks[my_id].entry) {
            tasks[my_id].entry();
        }

        // Task finished — mark as dead and yield
        tasks[my_id].state = TASK_DEAD;
        if (task_count > 0) task_count--;

        // Yield via INT 0x80 — never returns
        asm volatile("int $0x80");
        for (;;) asm volatile("hlt");
    }

    // ================================================================
    // Internal: pick next READY task (round-robin)
    // ================================================================
    static uint32_t pick_next() {
        uint32_t start = (current_task + 1) % MAX_TASKS;
        uint32_t i = start;
        do {
            if (i != idle_task_id && tasks[i].state == TASK_READY) {
                return i;
            }
            i = (i + 1) % MAX_TASKS;
        } while (i != start);

        // No ready task found — use idle task
        return idle_task_id;
    }

    // ================================================================
    // Internal: perform the actual context switch
    // ================================================================
    static uint64_t do_schedule(uint64_t current_rsp) {
        // Save current task
        tasks[current_task].rsp = current_rsp;
        if (tasks[current_task].state == TASK_RUNNING) {
            tasks[current_task].state = TASK_READY;
        }

        // Pick next
        uint32_t next = pick_next();

        // If only the current task is ready, stay on it
        if (next == current_task && tasks[current_task].state == TASK_READY) {
            tasks[current_task].state = TASK_RUNNING;
            tasks[current_task].slice_remaining = TIME_SLICE_MS;
            return current_rsp;
        }

        // Switch
        current_task = next;
        tasks[next].state = TASK_RUNNING;
        tasks[next].slice_remaining = TIME_SLICE_MS;

        return tasks[next].rsp;
    }

    // ================================================================
    // Create a task with a fake InterruptFrame on its stack
    // ================================================================
    static int create_task_internal(const char* name, void (*entry)(), bool is_idle) {
        int slot = -1;
        uint32_t start = is_idle ? 0 : 1;
        for (uint32_t i = start; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_UNUSED) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) return -1;

        uint32_t s = (uint32_t)slot;
        tasks[s].entry = entry;
        tasks[s].state = TASK_READY;
        tasks[s].id = next_id++;
        tasks[s].ticks_used = 0;
        tasks[s].wake_at = 0;
        tasks[s].slice_remaining = TIME_SLICE_MS;

        // Copy name
        int j = 0;
        while (name && name[j] && j < 31) {
            tasks[s].name[j] = name[j];
            j++;
        }
        tasks[s].name[j] = '\0';

        // Build a fake InterruptFrame at the top of the task's stack.
        // When the scheduler returns this RSP, irq_common will:
        //   pop 15 GP registers (all zeros)
        //   add rsp, 16 (skip int_no + error_code)
        //   iretq (pop RIP, CS, RFLAGS, RSP, SS)
        // ...landing at task_entry_wrapper with interrupts enabled.
        uint64_t stack_top = (uint64_t)&task_stacks[s][STACK_SIZE];
        InterruptFrame* frame = (InterruptFrame*)(stack_top - sizeof(InterruptFrame));

        // Zero the entire frame
        uint8_t* p = (uint8_t*)frame;
        for (size_t k = 0; k < sizeof(InterruptFrame); k++) p[k] = 0;

        // CPU state for iretq
        frame->rip    = (uint64_t)task_entry_wrapper;
        frame->cs     = 0x28;   // Kernel code segment (Limine GDT)
        frame->ss     = 0x30;   // Kernel data segment
        frame->rflags = 0x202;  // IF=1 (interrupts enabled)
        frame->rsp    = stack_top;

        tasks[s].rsp = (uint64_t)frame;
        task_count++;

        return slot;
    }

    // ================================================================
    // Public API
    // ================================================================

    bool init() {
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            tasks[i].state = TASK_UNUSED;
            tasks[i].rsp = 0;
            tasks[i].id = 0;
            tasks[i].entry = nullptr;
            tasks[i].ticks_used = 0;
            tasks[i].wake_at = 0;
            tasks[i].slice_remaining = TIME_SLICE_MS;
            for (int j = 0; j < 32; j++) tasks[i].name[j] = 0;
        }
        next_id = 0;

        // Task 0 = kernel/shell (already running on the boot stack).
        // Its RSP will be saved naturally when the first timer IRQ
        // preempts it. No fake InterruptFrame needed.
        tasks[0].state = TASK_RUNNING;
        tasks[0].id = next_id++;
        const char* n0 = "kernel";
        for (int i = 0; n0[i]; i++) tasks[0].name[i] = n0[i];
        tasks[0].slice_remaining = TIME_SLICE_MS;
        current_task = 0;
        task_count = 1;

        // Create the idle task (always slot 1)
        int idle_slot = create_task_internal("idle", idle_entry, true);
        if (idle_slot >= 0) {
            idle_task_id = (uint32_t)idle_slot;
        }

        initialized = true;

        console_print("\n[SCHED] Preemptive scheduler ready (");
        char buf[16];
        itoa(TIME_SLICE_MS, buf);
        console_print(buf);
        console_print("ms slice, ");
        itoa(MAX_TASKS, buf);
        console_print(buf);
        console_print(" max tasks)");

        return true;
    }

    int create_task(const char* name, void (*entry)()) {
        if (!initialized || !entry) return -1;
        int slot = create_task_internal(name, entry, false);
        if (slot >= 0) {
            char buf[16];
            console_print("\n[SCHED] Task ");
            itoa(tasks[slot].id, buf);
            console_print(buf);
            console_print(" created: ");
            console_print(tasks[slot].name);
        }
        return slot;
    }

    bool kill_task(uint32_t slot) {
        if (slot >= MAX_TASKS) return false;
        if (slot == 0) return false;           // Can't kill kernel
        if (slot == idle_task_id) return false; // Can't kill idle
        if (tasks[slot].state == TASK_UNUSED) return false;

        // Force to UNUSED immediately — the task will never be scheduled again.
        // This is abrupt (no cleanup) but guaranteed to stop it.
        tasks[slot].state = TASK_UNUSED;
        if (task_count > 0) task_count--;
        return true;
    }

    uint64_t timer_tick(uint64_t current_rsp) {
        if (!initialized) return current_rsp;

        // Wake sleeping tasks whose time has come
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_SLEEPING && ticks >= tasks[i].wake_at) {
                tasks[i].state = TASK_READY;
            }
        }

        // Clean up dead tasks
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_DEAD && i != current_task) {
                tasks[i].state = TASK_UNUSED;
            }
        }

        // Update stats
        tasks[current_task].ticks_used++;

        // If current task is no longer runnable (sleeping/dead), switch now
        if (tasks[current_task].state != TASK_RUNNING) {
            return do_schedule(current_rsp);
        }

        // Decrement time slice
        if (tasks[current_task].slice_remaining > 0) {
            tasks[current_task].slice_remaining--;
        }

        // Stay on current task if time slice not expired
        if (tasks[current_task].slice_remaining > 0) {
            return current_rsp;
        }

        // Time slice expired — context switch
        return do_schedule(current_rsp);
    }

    uint64_t yield(uint64_t current_rsp) {
        if (!initialized) return current_rsp;
        return do_schedule(current_rsp);
    }

    void sleep_ms(uint32_t ms) {
        if (!initialized) return;
        tasks[current_task].wake_at = ticks + ms;
        tasks[current_task].state = TASK_SLEEPING;
        asm volatile("int $0x80");
        // Resumes here after waking up
    }

    // ================================================================
    // Shell info API
    // ================================================================

    uint32_t get_count() { return task_count; }
    uint32_t get_current() { return current_task; }

    const Task* get_task(uint32_t idx) {
        if (idx >= MAX_TASKS) return nullptr;
        static Task view;
        view.rsp = tasks[idx].rsp;
        view.state = tasks[idx].state;
        view.id = tasks[idx].id;
        view.ticks_used = tasks[idx].ticks_used;
        view.wake_at = tasks[idx].wake_at;
        for (int i = 0; i < 32; i++) view.name[i] = tasks[idx].name[i];
        return &view;
    }
}

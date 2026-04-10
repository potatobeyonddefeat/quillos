#include "scheduler.h"
#include "idt.h"
#include "gdt.h"
#include "vmm.h"
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
        bool is_user;
        uint64_t cr3;       // 0 = kernel
        uint32_t pid;       // Process-manager pid, 0 if none
    };

    static TaskInternal tasks[MAX_TASKS];
    static uint32_t current_task = 0;
    static uint32_t task_count = 0;
    static uint32_t idle_task_id = 0;
    static uint32_t next_id = 0;
    static bool initialized = false;

    uint64_t kstack_top(uint32_t slot) {
        if (slot >= MAX_TASKS) return 0;
        return (uint64_t)&task_stacks[slot][STACK_SIZE];
    }

    InterruptFrame* saved_frame(uint32_t slot) {
        if (slot >= MAX_TASKS) return nullptr;
        return (InterruptFrame*)tasks[slot].rsp;
    }

    void set_slot_pid(uint32_t slot, uint32_t pid) {
        if (slot < MAX_TASKS) tasks[slot].pid = pid;
    }

    uint32_t get_slot_pid(uint32_t slot) {
        if (slot >= MAX_TASKS) return 0;
        return tasks[slot].pid;
    }

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
        asm volatile("sti");

        uint32_t my_id = current_task;
        if (tasks[my_id].entry) {
            tasks[my_id].entry();
        }

        tasks[my_id].state = TASK_DEAD;
        if (task_count > 0) task_count--;

        // Yield via INT 0x80 with SYS_YIELD in rax (0) — never returns
        asm volatile("mov $0, %%rax; int $0x80" ::: "rax");
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
        return idle_task_id;
    }

    // ================================================================
    // Internal: perform the actual context switch
    // ================================================================
    static uint64_t do_schedule(uint64_t current_rsp) {
        tasks[current_task].rsp = current_rsp;
        if (tasks[current_task].state == TASK_RUNNING) {
            tasks[current_task].state = TASK_READY;
        }

        uint32_t next = pick_next();

        if (next == current_task && tasks[current_task].state == TASK_READY) {
            tasks[current_task].state = TASK_RUNNING;
            tasks[current_task].slice_remaining = TIME_SLICE_MS;
            return current_rsp;
        }

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
        tasks[s].is_user = false;
        tasks[s].cr3 = 0;
        tasks[s].pid = 0;

        int j = 0;
        while (name && name[j] && j < 31) {
            tasks[s].name[j] = name[j];
            j++;
        }
        tasks[s].name[j] = '\0';

        uint64_t stack_top = (uint64_t)&task_stacks[s][STACK_SIZE];
        InterruptFrame* frame = (InterruptFrame*)(stack_top - sizeof(InterruptFrame));

        uint8_t* p = (uint8_t*)frame;
        for (size_t k = 0; k < sizeof(InterruptFrame); k++) p[k] = 0;

        frame->rip    = (uint64_t)task_entry_wrapper;
        frame->cs     = 0x28;
        frame->ss     = 0x30;
        frame->rflags = 0x202;
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
            tasks[i].is_user = false;
            tasks[i].cr3 = 0;
            tasks[i].pid = 0;
            for (int j = 0; j < 32; j++) tasks[i].name[j] = 0;
        }
        next_id = 0;

        tasks[0].state = TASK_RUNNING;
        tasks[0].id = next_id++;
        const char* n0 = "kernel";
        for (int i = 0; n0[i]; i++) tasks[0].name[i] = n0[i];
        tasks[0].slice_remaining = TIME_SLICE_MS;
        current_task = 0;
        task_count = 1;

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

    // ================================================================
    // User task creation
    // ================================================================

    int create_user_task(const char* name,
                         uint64_t cr3,
                         uint64_t user_entry,
                         uint64_t user_stack_top) {
        if (!initialized) return -1;

        int slot = -1;
        for (uint32_t i = 1; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_UNUSED) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) return -1;

        uint32_t s = (uint32_t)slot;
        tasks[s].entry = nullptr;
        tasks[s].state = TASK_READY;
        tasks[s].id = next_id++;
        tasks[s].ticks_used = 0;
        tasks[s].wake_at = 0;
        tasks[s].slice_remaining = TIME_SLICE_MS;
        tasks[s].is_user = true;
        tasks[s].cr3 = cr3;
        tasks[s].pid = 0;

        int j = 0;
        while (name && name[j] && j < 31) {
            tasks[s].name[j] = name[j];
            j++;
        }
        tasks[s].name[j] = '\0';

        uint64_t kstack = (uint64_t)&task_stacks[s][STACK_SIZE];
        InterruptFrame* frame = (InterruptFrame*)(kstack - sizeof(InterruptFrame));
        uint8_t* p = (uint8_t*)frame;
        for (size_t k = 0; k < sizeof(InterruptFrame); k++) p[k] = 0;

        // Build an iretq frame that returns to ring 3.
        frame->rip    = user_entry;
        frame->cs     = 0x3B;                // USER_CS | 3
        frame->rflags = 0x202;               // IF=1
        frame->rsp    = user_stack_top;
        frame->ss     = 0x43;                // USER_DS | 3

        tasks[s].rsp = (uint64_t)frame;
        task_count++;

        char buf[16];
        console_print("\n[SCHED] User task ");
        itoa(tasks[s].id, buf); console_print(buf);
        console_print(" created: ");
        console_print(tasks[s].name);

        return slot;
    }

    int fork_user_task(const char* name,
                       uint64_t cr3,
                       const InterruptFrame* parent_frame) {
        if (!initialized || !parent_frame) return -1;

        int slot = -1;
        for (uint32_t i = 1; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_UNUSED) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) return -1;

        uint32_t s = (uint32_t)slot;
        tasks[s].entry = nullptr;
        tasks[s].state = TASK_READY;
        tasks[s].id = next_id++;
        tasks[s].ticks_used = 0;
        tasks[s].wake_at = 0;
        tasks[s].slice_remaining = TIME_SLICE_MS;
        tasks[s].is_user = true;
        tasks[s].cr3 = cr3;
        tasks[s].pid = 0;

        int j = 0;
        while (name && name[j] && j < 31) {
            tasks[s].name[j] = name[j];
            j++;
        }
        tasks[s].name[j] = '\0';

        uint64_t kstack = (uint64_t)&task_stacks[s][STACK_SIZE];
        InterruptFrame* frame = (InterruptFrame*)(kstack - sizeof(InterruptFrame));

        // Copy the parent's interrupt frame verbatim
        const uint8_t* src = (const uint8_t*)parent_frame;
        uint8_t*       dst = (uint8_t*)frame;
        for (size_t k = 0; k < sizeof(InterruptFrame); k++) dst[k] = src[k];

        // Child receives 0 from fork()
        frame->rax = 0;

        tasks[s].rsp = (uint64_t)frame;
        task_count++;

        return slot;
    }

    bool kill_task(uint32_t slot) {
        if (slot >= MAX_TASKS) return false;
        if (slot == 0) return false;
        if (slot == idle_task_id) return false;
        if (tasks[slot].state == TASK_UNUSED) return false;

        tasks[slot].state = TASK_UNUSED;
        if (task_count > 0) task_count--;
        return true;
    }

    uint64_t timer_tick(uint64_t current_rsp) {
        if (!initialized) return current_rsp;

        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_SLEEPING && ticks >= tasks[i].wake_at) {
                tasks[i].state = TASK_READY;
            }
        }

        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_DEAD && i != current_task) {
                tasks[i].state = TASK_UNUSED;
            }
        }

        tasks[current_task].ticks_used++;

        if (tasks[current_task].state != TASK_RUNNING) {
            return do_schedule(current_rsp);
        }

        if (tasks[current_task].slice_remaining > 0) {
            tasks[current_task].slice_remaining--;
        }

        if (tasks[current_task].slice_remaining > 0) {
            return current_rsp;
        }

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
        asm volatile("mov $0, %%rax; int $0x80" ::: "rax");
    }

    uint64_t sleep_ms_from_frame(uint32_t ms, uint64_t frame_rsp) {
        if (!initialized) return frame_rsp;
        tasks[current_task].wake_at = ticks + ms;
        tasks[current_task].state = TASK_SLEEPING;
        return do_schedule(frame_rsp);
    }

    uint64_t exit_current(uint64_t frame_rsp) {
        if (!initialized) return frame_rsp;

        uint32_t me = current_task;
        tasks[me].state = TASK_DEAD;
        if (task_count > 0) task_count--;

        // Force a reschedule. do_schedule will save frame_rsp into
        // tasks[me].rsp but that's ok -- the task is dead and will
        // be swept on the next tick.
        uint64_t new_rsp = do_schedule(frame_rsp);

        // If for some reason we ended up back on the dead task
        // (e.g. only the idle/dead task existed), force idle.
        if (current_task == me) {
            current_task = idle_task_id;
            tasks[idle_task_id].state = TASK_RUNNING;
            new_rsp = tasks[idle_task_id].rsp;
        }
        return new_rsp;
    }

    void apply_task_context() {
        uint32_t s = current_task;
        // Update TSS.rsp0 for the next ring0 entry from this task
        GDT::set_kernel_stack(kstack_top(s));

        // Switch CR3 if needed
        uint64_t desired = tasks[s].cr3 ? tasks[s].cr3 : VMM::kernel_cr3();
        uint64_t cur;
        asm volatile("mov %%cr3, %0" : "=r"(cur));
        if ((cur & ~0xFFFULL) != (desired & ~0xFFFULL)) {
            VMM::switch_to(desired);
        }
    }

    // Used by exec() to swap the address space of the running task
    // without killing/recreating it.
    void retarget_current_cr3(uint64_t new_cr3) {
        tasks[current_task].cr3 = new_cr3;
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
        view.is_user = tasks[idx].is_user;
        view.cr3 = tasks[idx].cr3;
        for (int i = 0; i < 32; i++) view.name[i] = tasks[idx].name[i];
        return &view;
    }
}

// C-linkage shim so process.cpp can retarget without including
// the full Scheduler header graph.
extern "C" void sched_retarget_current_cr3(uint64_t new_cr3) {
    Scheduler::retarget_current_cr3(new_cr3);
}

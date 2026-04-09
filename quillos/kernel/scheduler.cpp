#include "scheduler.h"

extern "C" void context_switch(uint64_t* old_rsp, uint64_t new_rsp);
extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Scheduler {

    static constexpr uint64_t STACK_SIZE = 8192;

    // Per-task stacks (static allocation — no malloc needed)
    static uint8_t task_stacks[MAX_TASKS][STACK_SIZE]
        __attribute__((aligned(16)));

    static struct TaskInternal {
        uint64_t rsp;
        TaskState state;
        char name[32];
        uint64_t id;
        void (*entry)();
    } tasks[MAX_TASKS];

    static uint32_t current_task = 0;
    static uint32_t task_count = 0;
    static bool initialized = false;

    // Wrapper: runs the task entry function, then marks task done
    static void task_entry_wrapper() {
        if (tasks[current_task].entry)
            tasks[current_task].entry();

        // Task finished — mark as unused and yield
        tasks[current_task].state = TASK_UNUSED;
        if (task_count > 0) task_count--;
        yield();
        // Should never reach here
        for (;;) asm volatile("hlt");
    }

    bool init() {
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            tasks[i].state = TASK_UNUSED;
            tasks[i].id = 0;
            tasks[i].rsp = 0;
            tasks[i].entry = nullptr;
            for (int j = 0; j < 32; j++) tasks[i].name[j] = 0;
        }

        // Task 0 = kernel/shell (already running, uses kernel stack)
        tasks[0].state = TASK_RUNNING;
        tasks[0].id = 0;
        const char* n = "kernel";
        for (int i = 0; n[i]; i++) tasks[0].name[i] = n[i];

        task_count = 1;
        current_task = 0;
        initialized = true;

        console_print("\n[SCHED] Scheduler initialized (cooperative)");
        return true;
    }

    int create_task(const char* name, void (*entry)()) {
        if (!initialized || !entry) return -1;

        for (uint32_t i = 1; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_UNUSED) {
                tasks[i].state = TASK_READY;
                tasks[i].id = i;
                tasks[i].entry = entry;

                // Copy name
                int j = 0;
                while (name && name[j] && j < 31) {
                    tasks[i].name[j] = name[j];
                    j++;
                }
                tasks[i].name[j] = '\0';

                // Set up stack as if context_switch had saved callee-saved regs
                // Stack grows downward. Place return address + 6 saved regs.
                uint64_t* sp = (uint64_t*)&task_stacks[i][STACK_SIZE];
                *(--sp) = (uint64_t)task_entry_wrapper; // return address
                *(--sp) = 0; // rbx
                *(--sp) = 0; // rbp
                *(--sp) = 0; // r12
                *(--sp) = 0; // r13
                *(--sp) = 0; // r14
                *(--sp) = 0; // r15
                tasks[i].rsp = (uint64_t)sp;

                task_count++;

                char buf[32];
                console_print("\n[SCHED] Created task ");
                itoa(i, buf);
                console_print(buf);
                console_print(": ");
                console_print(tasks[i].name);

                return (int)i;
            }
        }
        return -1;
    }

    void yield() {
        if (!initialized || task_count <= 1) return;

        uint32_t old = current_task;

        // Round-robin: find next ready/running task
        uint32_t next = (current_task + 1) % MAX_TASKS;
        for (uint32_t tries = 0; tries < MAX_TASKS; tries++) {
            if (next != old &&
                (tasks[next].state == TASK_READY ||
                 tasks[next].state == TASK_RUNNING)) {
                break;
            }
            next = (next + 1) % MAX_TASKS;
        }

        if (next == old) return; // No other runnable tasks

        if (tasks[old].state == TASK_RUNNING)
            tasks[old].state = TASK_READY;

        current_task = next;
        tasks[next].state = TASK_RUNNING;

        context_switch(&tasks[old].rsp, tasks[next].rsp);
    }

    uint32_t get_count() { return task_count; }
    uint32_t get_current() { return current_task; }

    const Task* get_task(uint32_t idx) {
        if (idx >= MAX_TASKS) return nullptr;
        // Return a view of the internal task
        static Task view;
        view.rsp = tasks[idx].rsp;
        view.state = tasks[idx].state;
        view.id = tasks[idx].id;
        for (int i = 0; i < 32; i++) view.name[i] = tasks[idx].name[i];
        return &view;
    }
}

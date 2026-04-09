#pragma once
#include <stdint.h>

namespace Scheduler {
    enum TaskState : uint8_t {
        TASK_UNUSED = 0,
        TASK_READY,
        TASK_RUNNING,
        TASK_BLOCKED,
    };

    struct Task {
        uint64_t rsp;
        TaskState state;
        char name[32];
        uint64_t id;
    };

    bool init();
    int create_task(const char* name, void (*entry)());
    void yield();
    uint32_t get_count();
    uint32_t get_current();
    const Task* get_task(uint32_t idx);
    static constexpr uint32_t MAX_TASKS = 8;
}

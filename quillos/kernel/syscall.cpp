#include "syscall.h"
#include "idt.h"
#include "scheduler.h"
#include "process.h"
#include "vmm.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Syscall {

    static void log_unknown(uint64_t num) {
        char buf[32];
        console_print("\n[SYSCALL] unknown number ");
        itoa(num, buf);
        console_print(buf);
    }

    uint64_t dispatch(InterruptFrame* frame) {
        uint64_t num = frame->rax;
        uint64_t a1  = frame->rdi;
        uint64_t a2  = frame->rsi;
        uint64_t a3  = frame->rdx;

        switch (num) {
            case SYS_YIELD:
                return Scheduler::yield((uint64_t)frame);

            case SYS_EXIT: {
                uint64_t new_rsp = Process::sys_exit((int32_t)a1, frame);
                // After exit, the scheduler picked a new task — make
                // sure its CR3 + TSS.rsp0 are live before returning.
                Scheduler::apply_task_context();
                return new_rsp;
            }

            case SYS_WRITE: {
                uint64_t ret = Process::sys_write((int)a1, (const char*)a2, a3);
                frame->rax = ret;
                return (uint64_t)frame;
            }

            case SYS_READ: {
                uint64_t ret = Process::sys_read((int)a1, (char*)a2, a3);
                frame->rax = ret;
                return (uint64_t)frame;
            }

            case SYS_SLEEP: {
                uint64_t new_rsp = Process::sys_sleep_ms((uint32_t)a1, frame);
                Scheduler::apply_task_context();
                return new_rsp;
            }

            case SYS_FORK: {
                int32_t pid = Process::sys_fork(frame);
                frame->rax = (uint64_t)(int64_t)pid;
                return (uint64_t)frame;
            }

            case SYS_EXEC: {
                int32_t ret = Process::sys_exec((const char*)a1, frame);
                frame->rax = (uint64_t)(int64_t)ret;
                return (uint64_t)frame;
            }

            case SYS_GETPID: {
                frame->rax = (uint64_t)(int64_t)Process::sys_getpid();
                return (uint64_t)frame;
            }

            default:
                log_unknown(num);
                frame->rax = (uint64_t)-1;
                return (uint64_t)frame;
        }
    }

} // namespace Syscall

extern "C" uint64_t syscall_dispatch(InterruptFrame* frame) {
    return Syscall::dispatch(frame);
}

#pragma once
#include <stdint.h>

struct InterruptFrame;

// ================================================================
// QuillOS system call interface (int 0x80)
//
// Calling convention (similar to Linux x86_64 but via int 0x80):
//   RAX = syscall number
//   RDI = arg1
//   RSI = arg2
//   RDX = arg3
//   R10 = arg4
//   R8  = arg5
//   R9  = arg6
// Return in RAX.
// ================================================================

namespace Syscall {

    enum Number : uint64_t {
        SYS_YIELD  = 0,     // Scheduler yield (legacy)
        SYS_EXIT   = 1,     // exit(int status)
        SYS_WRITE  = 2,     // write(int fd, const char* buf, size_t len)
        SYS_SLEEP  = 3,     // sleep(uint32_t ms)
        SYS_FORK   = 4,     // fork() -> pid (parent) or 0 (child)
        SYS_EXEC   = 5,     // exec(const char* path)
        SYS_GETPID = 6,     // getpid() -> pid
        SYS_READ   = 7,     // read(int fd, char* buf, size_t len)
        SYS_MAX
    };

    // Called from int 0x80 stub (via irq_dispatch path).
    // Returns the RSP to iretq back to (may have been changed by
    // a context switch, e.g. via exit or sleep).
    uint64_t dispatch(InterruptFrame* frame);

} // namespace Syscall

// C-linkage shim called from interrupts.asm
extern "C" uint64_t syscall_dispatch(InterruptFrame* frame);

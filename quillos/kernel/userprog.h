#pragma once
#include <stdint.h>

// ================================================================
// Embedded user-space test programs.
//
// QuillOS doesn't yet have a userland toolchain baked into the
// build, so the first ring-3 programs ship as tiny hand-assembled
// blobs inside the kernel image. They are loaded into a fresh
// user address space by Process::spawn_user_blob and exercise
// the syscall interface.
// ================================================================

namespace UserProg {

    // Spawn the "hello" program. Prints a banner from ring 3 via
    // SYS_WRITE, then exits via SYS_EXIT. Returns the new pid, or
    // 0 on failure.
    uint32_t spawn_hello();

    // Spawn a ring-3 program that loops N times printing a
    // counter via SYS_WRITE + SYS_SLEEP, then exits.
    uint32_t spawn_ticker();

    // Spawn a ring-3 program that calls SYS_FORK and both parent
    // and child print who they are before exiting.
    uint32_t spawn_forker();

} // namespace UserProg

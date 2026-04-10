#pragma once
#include <stdint.h>

// ================================================================
// Global Descriptor Table with kernel + user segments and a TSS
//
// Slots (byte offsets) — chosen so the existing kernel selectors
// (0x28 kernel code, 0x30 kernel data) keep working, and new
// user entries live higher up.
//
//   0x00: Null
//   0x28: Kernel code (ring 0)
//   0x30: Kernel data (ring 0)
//   0x38: User code   (ring 3)   -> 0x3B with RPL=3
//   0x40: User data   (ring 3)   -> 0x43 with RPL=3
//   0x48: TSS low  (16 bytes in long mode)
//   0x50: TSS high
// ================================================================

namespace GDT {

    static constexpr uint16_t KERNEL_CS = 0x28;
    static constexpr uint16_t KERNEL_DS = 0x30;
    static constexpr uint16_t USER_CS   = 0x3B;   // 0x38 | 3
    static constexpr uint16_t USER_DS   = 0x43;   // 0x40 | 3
    static constexpr uint16_t TSS_SEL   = 0x48;

    void init();

    // Set the ring-0 stack the CPU switches to on interrupt/syscall
    // from ring 3. Scheduler calls this whenever it resumes a user task.
    void set_kernel_stack(uint64_t rsp);
}

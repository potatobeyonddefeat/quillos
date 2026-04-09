#pragma once
#include <stdint.h>

// ============================================================
// InterruptFrame — Pushed by CPU + assembly stubs + common handler
// This is the exact stack layout when our C dispatcher is called.
// ============================================================
struct InterruptFrame {
    // Pushed by isr_common / irq_common (in reverse order)
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    // Pushed by stub
    uint64_t int_no;
    uint64_t error_code;
    // Pushed by CPU on interrupt
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

// IRQ handler callback type
typedef void (*irq_handler_t)(InterruptFrame* frame);

// ============================================================
// Public API
// ============================================================

// Initialize IDT, PIC, install all ISR/IRQ stubs, enable interrupts
void idt_init();

// Register a handler for a hardware IRQ line (0-15)
// IRQ 0 = timer, IRQ 1 = keyboard, IRQ 14 = primary ATA, etc.
void irq_register(uint8_t irq, irq_handler_t handler);

// Unregister a handler
void irq_unregister(uint8_t irq);

// Mask (disable) or unmask (enable) a specific IRQ line
void irq_mask(uint8_t irq);
void irq_unmask(uint8_t irq);

// Statistics
uint64_t irq_get_count(uint8_t irq);  // How many times IRQ N has fired
uint64_t isr_get_total_exceptions();   // Total CPU exceptions caught

// Get human-readable exception name
const char* isr_exception_name(uint8_t num);

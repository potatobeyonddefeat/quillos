#include "idt.h"
#include "io.h"
#include "scheduler.h"
#include "syscall.h"
#include <stddef.h>

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

// ============================================================
// IDT structures
// ============================================================

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDTPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static IDTEntry idt[256];
static IDTPointer idt_ptr;

// ============================================================
// Assembly-defined symbols
// ============================================================

// Table of 48 stub addresses (32 ISRs + 16 IRQs), defined in interrupts.asm
extern "C" void* isr_stub_table[];

// Load IDT register
extern "C" void load_idt(void* ptr);

// INT 0x80 syscall stub (defined in interrupts.asm)
extern "C" void sched_yield_stub();

// C-callable dispatchers (called from assembly common handlers)
extern "C" void isr_dispatch(InterruptFrame* frame);
extern "C" uint64_t irq_dispatch(InterruptFrame* frame);
extern "C" uint64_t syscall_dispatch(InterruptFrame* frame);

// ============================================================
// IRQ handler table + statistics
// ============================================================

static irq_handler_t irq_handlers[16] = { nullptr };
static uint64_t irq_counts[16] = { 0 };
static uint64_t total_exceptions = 0;

// ============================================================
// Exception names (ISR 0-31)
// ============================================================

static const char* exception_names[] = {
    "Divide by Zero",            // 0
    "Debug",                     // 1
    "Non-Maskable Interrupt",    // 2
    "Breakpoint",                // 3
    "Overflow",                  // 4
    "Bound Range Exceeded",      // 5
    "Invalid Opcode",            // 6
    "Device Not Available",      // 7
    "Double Fault",              // 8
    "Coprocessor Segment Overrun", // 9
    "Invalid TSS",               // 10
    "Segment Not Present",       // 11
    "Stack-Segment Fault",       // 12
    "General Protection Fault",  // 13
    "Page Fault",                // 14
    "Reserved",                  // 15
    "x87 FPU Error",             // 16
    "Alignment Check",           // 17
    "Machine Check",             // 18
    "SIMD FPU Exception",        // 19
    "Virtualization Exception",  // 20
    "Control Protection",        // 21
    "Reserved",                  // 22
    "Reserved",                  // 23
    "Reserved",                  // 24
    "Reserved",                  // 25
    "Reserved",                  // 26
    "Reserved",                  // 27
    "Hypervisor Injection",      // 28
    "VMM Communication",         // 29
    "Security Exception",        // 30
    "Reserved",                  // 31
};

const char* isr_exception_name(uint8_t num) {
    if (num < 32) return exception_names[num];
    return "Unknown";
}

// ============================================================
// IDT entry setup
// ============================================================

static void set_idt_entry(int num, void* handler, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[num].offset_low  = addr & 0xFFFF;
    idt[num].selector    = 0x28;  // Kernel code segment from Limine GDT
    idt[num].ist         = 0;
    idt[num].type_attr   = type_attr;
    idt[num].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[num].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[num].zero        = 0;
}

// ============================================================
// PIC (8259) remapping and control
// ============================================================

static void pic_remap() {
    // Save masks
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);

    // ICW1: begin initialization (cascade mode, ICW4 needed)
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();

    // ICW2: vector offsets
    outb(0x21, 0x20); io_wait();  // Master: IRQ 0-7  -> INT 32-39
    outb(0xA1, 0x28); io_wait();  // Slave:  IRQ 8-15 -> INT 40-47

    // ICW3: cascade wiring
    outb(0x21, 0x04); io_wait();  // Master: slave on IRQ 2
    outb(0xA1, 0x02); io_wait();  // Slave:  cascade identity 2

    // ICW4: 8086 mode
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    // Start with all IRQs masked — we'll unmask as handlers register
    (void)mask1;
    (void)mask2;
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void irq_mask(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }
    uint8_t val = inb(port) | (1 << irq);
    outb(port, val);
}

void irq_unmask(uint8_t irq) {
    uint16_t port;
    uint8_t line = irq;
    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        line -= 8;
    }
    uint8_t val = inb(port) & ~(1 << line);
    outb(port, val);

    // If unmasking a slave IRQ, also unmask the cascade line (IRQ 2)
    if (irq >= 8) {
        uint8_t master = inb(0x21) & ~(1 << 2);
        outb(0x21, master);
    }
}

static void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);  // EOI to slave
    }
    outb(0x20, 0x20);      // EOI to master (always)
}

// ============================================================
// Handler registration
// ============================================================

void irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq >= 16) return;
    irq_handlers[irq] = handler;
    irq_unmask(irq);
}

void irq_unregister(uint8_t irq) {
    if (irq >= 16) return;
    irq_mask(irq);
    irq_handlers[irq] = nullptr;
}

// ============================================================
// C Dispatch functions (called from assembly)
// ============================================================

// Helper to print a 64-bit hex value
static void print_hex64(uint64_t val) {
    const char* hex = "0123456789ABCDEF";
    char buf[19]; // "0x" + 16 hex digits + null
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[18] = '\0';
    console_print(buf);
}

extern "C" void isr_dispatch(InterruptFrame* frame) {
    total_exceptions++;

    uint64_t num = frame->int_no;

    // Print a crash banner with register dump
    console_print("\n\n=== KERNEL EXCEPTION ===");
    console_print("\n  Exception: ");
    if (num < 32) console_print(exception_names[num]);
    console_print(" (INT ");
    char buf[16];
    itoa(num, buf);
    console_print(buf);
    console_print(")");

    console_print("\n  Error code: ");
    print_hex64(frame->error_code);

    console_print("\n  RIP: "); print_hex64(frame->rip);
    console_print("  CS:  "); print_hex64(frame->cs);
    console_print("\n  RSP: "); print_hex64(frame->rsp);
    console_print("  SS:  "); print_hex64(frame->ss);
    console_print("\n  RFLAGS: "); print_hex64(frame->rflags);

    console_print("\n  RAX: "); print_hex64(frame->rax);
    console_print("  RBX: "); print_hex64(frame->rbx);
    console_print("\n  RCX: "); print_hex64(frame->rcx);
    console_print("  RDX: "); print_hex64(frame->rdx);
    console_print("\n  RSI: "); print_hex64(frame->rsi);
    console_print("  RDI: "); print_hex64(frame->rdi);
    console_print("\n  RBP: "); print_hex64(frame->rbp);
    console_print("\n  R8:  "); print_hex64(frame->r8);
    console_print("  R9:  "); print_hex64(frame->r9);
    console_print("\n  R10: "); print_hex64(frame->r10);
    console_print("  R11: "); print_hex64(frame->r11);
    console_print("\n  R12: "); print_hex64(frame->r12);
    console_print("  R13: "); print_hex64(frame->r13);
    console_print("\n  R14: "); print_hex64(frame->r14);
    console_print("  R15: "); print_hex64(frame->r15);

    if (num == 14) {
        // Page fault: CR2 holds the faulting address
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        console_print("\n  CR2 (fault addr): "); print_hex64(cr2);
    }

    console_print("\n\n  System halted. Press reset to reboot.");
    asm volatile("cli; hlt");
}

extern "C" uint64_t irq_dispatch(InterruptFrame* frame) {
    uint8_t irq = (uint8_t)(frame->int_no - 32);

    if (irq < 16) {
        irq_counts[irq]++;

        // Call registered handler if one exists
        if (irq_handlers[irq]) {
            irq_handlers[irq](frame);
        }

        // Send End-of-Interrupt to PIC
        pic_send_eoi(irq);

        // Timer IRQ: let the scheduler potentially switch tasks.
        if (irq == 0) {
            uint64_t new_rsp = Scheduler::timer_tick((uint64_t)frame);
            // Apply per-task CR3 + TSS.rsp0 for whatever task is
            // about to resume.
            Scheduler::apply_task_context();
            return new_rsp;
        }
    }

    return (uint64_t)frame;
}

// ============================================================
// Statistics
// ============================================================

uint64_t irq_get_count(uint8_t irq) {
    return irq < 16 ? irq_counts[irq] : 0;
}

uint64_t isr_get_total_exceptions() {
    return total_exceptions;
}

// ============================================================
// Initialization
// ============================================================

void idt_init() {
    // 1. Remap PIC first (before installing any handlers)
    pic_remap();

    // 2. Install ISR stubs (INT 0-31) as interrupt gates (0x8E)
    for (int i = 0; i < 32; i++) {
        set_idt_entry(i, isr_stub_table[i], 0x8E);
    }

    // 3. Install IRQ stubs (INT 32-47) as interrupt gates (0x8E)
    for (int i = 0; i < 16; i++) {
        set_idt_entry(32 + i, isr_stub_table[32 + i], 0x8E);
    }

    // 4. Install system call / yield interrupt (INT 0x80) with DPL=3
    //    so user code can invoke it (0xEE = P=1, DPL=3, type=0xE)
    set_idt_entry(0x80, (void*)sched_yield_stub, 0xEE);

    // 5. Load IDT
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;
    load_idt(&idt_ptr);

    // 5. Enable interrupts
    asm volatile("sti");
}

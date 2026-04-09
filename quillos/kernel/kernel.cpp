#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "console.h"
#include "idt.h"
#include "shell.h"
#include "memory.h"
#include "scheduler.h"
#include "pci.h"
#include "disk.h"
#include "network.h"
#include "cluster.h"

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL
};

// Defined in timer.cpp and keyboard.cpp
extern "C" void init_pit();
extern void keyboard_init();

extern "C" void _start(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        for(;;) asm volatile("hlt");
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    console_init((uint32_t*)fb->address, fb->pitch);
    console_print("QuillOS v0.1 booting...");

    // 1. IDT + PIC — must be first so IRQ registration works
    idt_init();
    console_print("\n[OK] IDT + PIC initialized (32 ISRs, 16 IRQs)");

    // 2. Timer — registers IRQ 0 handler + unmasks it
    init_pit();
    console_print("\n[OK] PIT timer at ~1000 Hz (IRQ 0)");

    // 3. Keyboard — registers IRQ 1 handler + unmasks it
    keyboard_init();
    console_print("\n[OK] Keyboard driver (IRQ 1)");

    // 4. Memory manager
    Memory::init();

    // 5. Scheduler
    Scheduler::init();

    // 6. PCI bus enumeration
    PCI::init();

    // 7. Disk driver (ATA PIO)
    Disk::init();

    // 8. Network stack
    Network::init();

    // 9. Cluster system
    Cluster::init();

    // 10. Shell (also inits filesystem)
    console_print("\n");
    shell_init();

    for(;;) asm volatile("hlt");
}

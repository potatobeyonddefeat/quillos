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
#include "e1000.h"
#include "network.h"
#include "cluster.h"
#include "djob.h"
#include "process.h"
#include "cpu.h"
#include "users.h"

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL
};

extern "C" void init_pit();
extern void keyboard_init();

extern "C" void _start(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        for(;;) asm volatile("hlt");
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    console_init((uint32_t*)fb->address, fb->pitch);
    console_print("QuillOS v0.1 booting...");

    // 1. IDT + PIC
    idt_init();
    console_print("\n[OK] IDT + PIC initialized (32 ISRs, 16 IRQs)");

    // 2. Timer
    init_pit();
    console_print("\n[OK] PIT timer at ~1000 Hz (IRQ 0)");

    // 3. Keyboard
    keyboard_init();
    console_print("\n[OK] Keyboard driver (IRQ 1)");

    // 4. CPU info (CPUID)
    CPU::init();

    // 5. Memory manager (PMM + heap)
    Memory::init();

    // 5. Scheduler (preemptive, needed before cluster tasks)
    Scheduler::init();

    // 6. PCI bus
    PCI::init();

    // 7. Disk driver (ATA PIO)
    Disk::init();

    // 8. E1000 NIC driver (hardware init, DMA setup)
    E1000::init();

    // 9. Network protocol stack (Ethernet, ARP, IPv4, UDP)
    Network::init();

    // 10. Cluster system (discovery + job dispatch, starts background tasks)
    Cluster::init();

    // 11. Distributed job scheduler
    DJob::init();

    // 12. Process manager
    Process::init();

    // 13. User account system
    Users::init();

    // 14. Shell
    console_print("\n");
    shell_init();

    for(;;) asm volatile("hlt");
}

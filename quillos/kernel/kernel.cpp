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

extern "C" void init_pit();

extern "C" void _start(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        for(;;) asm volatile("hlt");
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    console_init((uint32_t*)fb->address, fb->pitch);
    console_print("QuillOS v0.1 booting...");

    // Core hardware
    init_pit();
    idt_init();

    // Memory manager
    Memory::init();

    // Scheduler (cooperative multitasking)
    Scheduler::init();

    // PCI bus enumeration
    PCI::init();

    // Disk driver (ATA PIO)
    Disk::init();

    // Network stack
    Network::init();

    // Cluster system
    Cluster::init();

    // Shell (also inits filesystem)
    console_print("\n");
    shell_init();

    for(;;) asm volatile("hlt");
}

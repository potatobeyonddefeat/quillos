#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "console.h"
#include "idt.h"
#include "shell.h" // Ensure this is included for shell_init()

// Limine requests
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL
};

void done() {
    for (;;) {
        __asm__ ("hlt");
    }
}

// Move this here so it can be called
void enable_sse() {
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((uint64_t)1 << 2); // Clear EM bit
    cr0 |= (uint64_t)1 << 1;    // Set MP bit
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (uint64_t)3 << 9;    // Set OSFXSR and OSXMMEXCPT bits
    asm volatile("mov %0, %%cr4" :: "r"(cr4));
}

extern "C" void _start(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        done();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    // 1. Initialize CPU features
    enable_sse(); 

    // 2. Initialize Hardware
    console_init((uint32_t*)fb->address, fb->pitch);
    idt_init();

    // 3. Launch Shell properly
    shell_init(); 

    done();
}

#include <stdint.h>
#include <stddef.h>
#include "limine.h"
#include "console.h"
#include "idt.h"

// Limine requests
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL
};

// Halts the CPU in a loop
void done() {
    for (;;) {
        __asm__ ("hlt");
    }
}

// Separate function for the shell logic
void start_shell() {
    console_print("\nQuillOS v0.1\n> ");
    
    // We stay here and let interrupts handle the typing
    for (;;) {
        __asm__ ("hlt");
    }
}

extern "C" void _start(void) {
    // 1. Ensure we have a framebuffer
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        done();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    // 2. Initialize Console
    console_init((uint32_t*)fb->address, fb->pitch);

    // 3. Initialize IDT (This calls pic_remap and sti)
    idt_init();

    // 4. Launch Shell
    start_shell();

    done();
}

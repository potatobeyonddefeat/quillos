#include "io.h"
#include "shell.h"
#include "idt.h"
#include <stdint.h>

static const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

// IRQ 1 handler — called by the interrupt dispatcher (EOI is automatic)
static void keyboard_irq_handler(InterruptFrame*) {
    uint8_t scancode = inb(0x60);

    // Only process key-down events (high bit clear)
    if (!(scancode & 0x80)) {
        if (scancode < sizeof(scancode_to_ascii)) {
            char c = scancode_to_ascii[scancode];
            if (c > 0) {
                shell_update(c);
            }
        }
    }
}

// Called from init_pit or kernel.cpp to register the keyboard IRQ
void keyboard_init() {
    irq_register(1, keyboard_irq_handler);
}

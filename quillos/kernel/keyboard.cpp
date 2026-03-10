#include "console.h"
#include "io.h"
#include <stdint.h>

const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

extern "C" void keyboard_handler_main() {
    uint8_t scancode = inb(0x60);

    // If bit 7 is clear, it's a "press" event
    if (!(scancode & 0x80)) {
        if (scancode < sizeof(scancode_to_ascii)) {
            char c = scancode_to_ascii[scancode];
            if (c > 0) {
                console_putc(c);
            }
        }
    }

    // Send End of Interrupt to Master PIC
    outb(0x20, 0x20);
}

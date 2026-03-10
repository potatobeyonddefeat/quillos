#include "io.h"
#include "shell.h"
#include <stdint.h>

const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

extern "C" void keyboard_handler_main() {
    // 1. Read data
    uint8_t scancode = inb(0x60);
    
    // 2. Send EOI immediately so PIC can handle future interrupts
    outb(0x20, 0x20); 

    // 3. Process key
    if (!(scancode & 0x80)) {
        if (scancode < sizeof(scancode_to_ascii)) {
            char c = scancode_to_ascii[scancode];
            if (c > 0) {
                shell_update(c);
            }
        }
    }
}

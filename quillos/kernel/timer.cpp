#include <stdint.h>
#include "io.h"

volatile uint64_t ticks = 0;

extern "C" {
    void init_pit() {
        uint16_t divisor = 1193;
        outb(0x43, 0x36);
        outb(0x40, (uint8_t)(divisor & 0xFF));
        outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    }

    void timer_handler_main() {
        ticks++;
        outb(0x20, 0x20);
    }
}

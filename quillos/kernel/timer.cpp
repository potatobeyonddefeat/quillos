#include <stdint.h>
#include "io.h"
#include "idt.h"

volatile uint64_t ticks = 0;

// IRQ 0 handler — called by the interrupt dispatcher (EOI is automatic)
static void timer_irq_handler(InterruptFrame*) {
    ticks++;
}

extern "C" void init_pit() {
    // Configure PIT channel 0 in rate generator mode
    // Divisor 1193 = ~1000 Hz (1193182 / 1193 ≈ 1000.15 Hz)
    uint16_t divisor = 1193;
    outb(0x43, 0x36);                         // Channel 0, lobyte/hibyte, rate generator
    outb(0x40, (uint8_t)(divisor & 0xFF));     // Low byte
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF)); // High byte

    // Register our handler on IRQ 0 (this also unmasks it)
    irq_register(0, timer_irq_handler);
}

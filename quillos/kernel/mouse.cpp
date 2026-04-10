#include "mouse.h"
#include "idt.h"
#include "io.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Mouse {

    static constexpr uint16_t PS2_DATA = 0x60;
    static constexpr uint16_t PS2_STAT = 0x64;
    static constexpr uint16_t PS2_CMD  = 0x64;

    static int32_t mouse_x = 512;
    static int32_t mouse_y = 384;
    static uint8_t buttons = 0;
    static uint64_t packet_count = 0;
    static bool ready = false;

    static uint8_t packet[3];
    static int     packet_idx = 0;

    static void ps2_wait_write() {
        for (int i = 0; i < 100000; i++) {
            if ((inb(PS2_STAT) & 0x02) == 0) return;
        }
    }

    static void ps2_wait_read() {
        for (int i = 0; i < 100000; i++) {
            if (inb(PS2_STAT) & 0x01) return;
        }
    }

    static void mouse_write(uint8_t byte) {
        ps2_wait_write();
        outb(PS2_CMD, 0xD4);
        ps2_wait_write();
        outb(PS2_DATA, byte);
    }

    static uint8_t mouse_read() {
        ps2_wait_read();
        return inb(PS2_DATA);
    }

    static void mouse_irq(InterruptFrame*) {
        uint8_t status = inb(PS2_STAT);
        if ((status & 0x21) != 0x21) return;

        uint8_t byte = inb(PS2_DATA);

        if (packet_idx == 0 && !(byte & 0x08)) return;

        packet[packet_idx++] = byte;
        if (packet_idx < 3) return;
        packet_idx = 0;

        uint8_t flags = packet[0];
        if (flags & 0xC0) return;

        int32_t dx = (int32_t)(int8_t)packet[1];
        int32_t dy = (int32_t)(int8_t)packet[2];

        mouse_x += dx;
        mouse_y -= dy;

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x > 1023) mouse_x = 1023;
        if (mouse_y > 767)  mouse_y = 767;

        buttons = flags & 0x07;
        packet_count++;
    }

    bool init() {
        ps2_wait_write();
        outb(PS2_CMD, 0xA8);

        ps2_wait_write();
        outb(PS2_CMD, 0x20);
        ps2_wait_read();
        uint8_t config = inb(PS2_DATA);

        config |= 0x02;
        config &= ~0x20;

        ps2_wait_write();
        outb(PS2_CMD, 0x60);
        ps2_wait_write();
        outb(PS2_DATA, config);

        mouse_write(0xF6);
        if (mouse_read() != 0xFA) {
            console_print("\n[MOUSE] Set defaults: no ACK");
            return false;
        }

        mouse_write(0xF4);
        if (mouse_read() != 0xFA) {
            console_print("\n[MOUSE] Enable reporting: no ACK");
            return false;
        }

        irq_register(12, mouse_irq);

        ready = true;
        console_print("\n[MOUSE] PS/2 mouse ready (IRQ 12)");
        return true;
    }

    int32_t get_x() { return mouse_x; }
    int32_t get_y() { return mouse_y; }
    uint8_t get_buttons() { return buttons; }
    uint64_t get_packet_count() { return packet_count; }
    bool is_present() { return ready; }
}

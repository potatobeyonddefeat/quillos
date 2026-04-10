#include "serial.h"
#include "io.h"

extern void console_print(const char* str);
extern volatile uint64_t ticks;

namespace Serial {

    static constexpr uint16_t PORT = 0x3F8;

    bool init() {
        outb(PORT + 1, 0x00);
        outb(PORT + 3, 0x80);
        outb(PORT + 0, 0x03);
        outb(PORT + 1, 0x00);
        outb(PORT + 3, 0x03);
        outb(PORT + 2, 0xC7);
        outb(PORT + 4, 0x0B);

        outb(PORT + 4, 0x1E);
        outb(PORT + 0, 0xAE);
        if (inb(PORT + 0) != 0xAE) {
            console_print("\n[SERIAL] Loopback test failed (COM1 absent?)");
            return false;
        }
        outb(PORT + 4, 0x0F);

        console_print("\n[SERIAL] COM1 ready (38400 8N1)");
        return true;
    }

    bool data_available() {
        return (inb(PORT + 5) & 1) != 0;
    }

    char read_char() {
        while (!data_available()) {
            asm volatile("pause");
        }
        return (char)inb(PORT);
    }

    static bool transmit_empty() {
        return (inb(PORT + 5) & 0x20) != 0;
    }

    void write_char(char c) {
        while (!transmit_empty()) {
            asm volatile("pause");
        }
        outb(PORT, (uint8_t)c);
    }

    void write_str(const char* s) {
        while (*s) write_char(*s++);
    }

    uint32_t read_available(char* buf, uint32_t buf_size) {
        uint32_t n = 0;
        while (n < buf_size - 1 && data_available()) {
            buf[n++] = (char)inb(PORT);
        }
        buf[n] = '\0';
        return n;
    }

    uint32_t read_line(char* buf, uint32_t buf_size, uint32_t timeout_ms) {
        uint32_t n = 0;
        uint64_t start = ticks;
        while (n < buf_size - 1) {
            if (data_available()) {
                char c = (char)inb(PORT);
                if (c == '\n' || c == '\r') break;
                buf[n++] = c;
            } else {
                if (ticks - start > timeout_ms) break;
                asm volatile("pause");
            }
        }
        buf[n] = '\0';
        return n;
    }
}

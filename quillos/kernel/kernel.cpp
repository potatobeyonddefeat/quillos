#include <limine.h>
#include <stdint.h>
#include <stddef.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

static void hcf() {
    for (;;) {
        asm("hlt");
    }
}

extern "C" void _start() {

    volatile uint16_t* vga = (uint16_t*)0xB8000;

    const char* msg =
        "QuillOS v0.1\n"
        "Developer Build\n\n"
        "> ";

    int row = 0;
    int col = 0;

    for (int i = 0; msg[i] != 0; i++) {

        if (msg[i] == '\n') {
            row++;
            col = 0;
            continue;
        }

        vga[row * 80 + col] = (0x0F << 8) | msg[i];
        col++;
    }

    hcf();
}

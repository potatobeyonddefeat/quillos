#include <stdint.h>
#include "console.h"
#include "font.h"

static uint32_t* framebuffer;
static uint32_t pitch;
static int cursor_x = 10;
static int cursor_y = 10;

// Adjust these based on your Limine initialization if needed
static const int SCREEN_WIDTH = 1024; 
static const int CHAR_W = 8;
static const int CHAR_H = 8;
static const uint32_t BG_COLOR = 0x0000FF; // Blue
static const uint32_t FG_COLOR = 0xFFFFFF; // White

void console_init(uint32_t* fb, uint64_t fb_pitch) {
    framebuffer = fb;
    pitch = fb_pitch / 4;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            framebuffer[(y + i) * pitch + (x + j)] = color;
        }
    }
}

void draw_char(char c, int x, int y, uint32_t color) {
    if ((unsigned char)c > 127) c = '?';
    for (int row = 0; row < 8; row++) {
        uint8_t line = font[(int)c][row];
        for (int col = 0; col < 8; col++) {
            if (line & (1 << col)) {
                framebuffer[(y + row) * pitch + (x + col)] = color;
            }
        }
    }
}

void console_putc(char c) {
    if (c == '\n') {
        cursor_x = 10;
        cursor_y += CHAR_H + 4;
    } else if (c == '\b') {
        if (cursor_x > 10) {
            cursor_x -= CHAR_W;
            draw_rect(cursor_x, cursor_y, CHAR_W, CHAR_H, BG_COLOR);
        }
    } else {
        draw_char(c, cursor_x, cursor_y, FG_COLOR);
        cursor_x += CHAR_W;
        if (cursor_x > SCREEN_WIDTH - 10) console_putc('\n');
    }
}

void console_print(const char* str) {
    while (*str) {
        console_putc(*str++);
    }
}

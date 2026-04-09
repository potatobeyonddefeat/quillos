#include <stdint.h>
#include "console.h"
#include "font.h"

static const int SCREEN_WIDTH = 1024;
static const int SCREEN_HEIGHT = 768;
static const int CHAR_W = 8;
static const int CHAR_H = 8;

static uint32_t* framebuffer;
static uint32_t pitch;
static int cursor_x = 10;
static int cursor_y = 10;
static uint32_t current_bg_color = 0x0000FF; 
static uint32_t current_fg_color = 0xFFFFFF; 

void console_init(uint32_t* fb, uint64_t fb_pitch) {
    framebuffer = fb;
    pitch = fb_pitch / 4;
    console_clear();
}

void set_bg_color(uint32_t color) {
    current_bg_color = color;
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

void console_clear() {
    for (uint64_t y = 0; y < SCREEN_HEIGHT; y++) {
        for (uint64_t x = 0; x < pitch; x++) {
             framebuffer[y * pitch + x] = current_bg_color;
        }
    }
    cursor_x = 10;
    cursor_y = 10;
}

void console_backspace() {
    if (cursor_x > 10) {
        cursor_x -= CHAR_W;
    } else if (cursor_y > 10) {
        cursor_y -= (CHAR_H + 4);
        cursor_x = ((SCREEN_WIDTH - 20) / CHAR_W) * CHAR_W;
    }
    draw_rect(cursor_x, cursor_y, CHAR_W, CHAR_H, current_bg_color);
}

void console_putc(char c) {
    if (c == '\n') {
        cursor_x = 10;
        cursor_y += CHAR_H + 4;
    } else if (c == '\b') {
        console_backspace();
    } else {
        draw_char(c, cursor_x, cursor_y, current_fg_color);
        cursor_x += CHAR_W;
        if (cursor_x > SCREEN_WIDTH - 10) console_putc('\n');
    }
}

void console_print(const char* str) {
    while (*str) {
        console_putc(*str++);
    }
}

#include <stdint.h>
#include "console.h"
#include "font.h"

static uint32_t* framebuffer;
static uint64_t pitch;

static int cursor_x = 0;
static int cursor_y = 0;

static const int CHAR_W = 8;
static const int CHAR_H = 8;

void console_init(uint32_t* fb, uint64_t p) {
    framebuffer = fb;
    pitch = p / 4;
    cursor_x = 10;
    cursor_y = 10;
}

static void draw_char(char c, int px, int py) {

    const uint8_t* glyph = 0;

    switch(c) {
        case 'Q': glyph = font_Q; break;
        case 'u': glyph = font_u; break;
        case 'i': glyph = font_i; break;
        case 'l': glyph = font_l; break;
        case 'O': glyph = font_O; break;
        case 'S': glyph = font_S; break;
        case 'v': glyph = font_v; break;
        case '.': glyph = font_dot; break;
        case '1': glyph = font_1; break;
        case '0': glyph = font_0; break;
        case 'D': glyph = font_D; break;
        case 'e': glyph = font_e; break;
        case 'p': glyph = font_p; break;
        case 'r': glyph = font_r; break;
        case 'B': glyph = font_B; break;
        case 'd': glyph = font_d; break;
        case '>': glyph = font_arrow; break;
        case ' ': glyph = font_space; break;
	case 'o': glyph = font_o; break;
	case 'b': glyph = font_b; break;
        default: return;
    }

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {

            if (glyph[y] & (1 << (7 - x))) {
                framebuffer[(py + y) * pitch + (px + x)] = 0xFFFFFFFF;
            }

        }
    }
}

void console_print(const char* str) {

    while (*str) {

        if (*str == '\n') {
            cursor_x = 10;
            cursor_y += CHAR_H + 2;
            str++;
            continue;
        }

        draw_char(*str, cursor_x, cursor_y);

        cursor_x += CHAR_W;

        str++;
    }
}

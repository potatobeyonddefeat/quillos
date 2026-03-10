#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

void console_init(uint32_t* fb, uint64_t fb_pitch);
void console_putc(char c);
void console_print(const char* str);
void console_backspace();
void console_clear();
void draw_rect(int x, int y, int w, int h, uint32_t color);

#endif

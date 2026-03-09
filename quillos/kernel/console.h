#pragma once
#include <stdint.h>

void console_init(uint32_t* fb, uint64_t pitch);
void console_putc(char c);
void console_print(const char* str);

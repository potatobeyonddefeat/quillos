#include "shell.h"
#include "kstring.h"

extern void console_print(const char* str); // Matches console.cpp
#define kprint console_print                // Let's us keep using kprint in this file
extern void console_clear();
extern void console_backspace(); // We'll define this in console.cpp

#define SHELL_BUFFER_SIZE 256
char shell_buffer[SHELL_BUFFER_SIZE];
int shell_ptr = 0;

void shell_init() {
    shell_ptr = 0;
    kprint("\nQuillOS v0.1\n> ");
}

void process_command(char* input) {
    if (strcmp(input, "help") == 0) {
        kprint("\nCommands: help, cls, ver, halt");
    } else if (strcmp(input, "cls") == 0) {
        console_clear();
    } else if (strcmp(input, "ver") == 0) {
        kprint("\nQuillOS v0.1 - Developer Build");
    } else if (strcmp(input, "halt") == 0) {
        kprint("\nSystem halting...");
        asm volatile("cli; hlt");
    } else if (strlen(input) > 0) {
        kprint("\nUnknown command: ");
        kprint(input);
    }
}

void shell_update(char c) {
    if (c == '\n') {
        shell_buffer[shell_ptr] = '\0';
        process_command(shell_buffer);
        shell_ptr = 0;
        kprint("\n> ");
    } else if (c == '\b') {
        if (shell_ptr > 0) {
            shell_ptr--;
            console_backspace();
        }
    } else {
        if (shell_ptr < SHELL_BUFFER_SIZE - 1) {
            shell_buffer[shell_ptr++] = c;
            char str[2] = {c, '\0'};
            kprint(str);
        }
    }
}

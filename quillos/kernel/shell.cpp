#include "shell.h"
#include "io.h"

extern void console_print(const char* str);
extern void console_clear();
extern void console_backspace();
extern void console_putc(char c);
extern void set_bg_color(uint32_t color);
extern void itoa(uint64_t n, char* str); // Declaration
extern volatile uint64_t ticks;

// Access the global tick counter from timer.cpp
extern volatile uint64_t ticks; 

#define kprint console_print
#define SHELL_BUFFER_SIZE 256
char shell_buffer[SHELL_BUFFER_SIZE];
int shell_ptr = 0;

// Internal safe string length
int safe_strlen(const char* s) {
    int len = 0;
    while (s[len] != '\0') len++;
    return len;
}

// Internal safe string compare
bool safe_compare(const char* a, const char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return false;
        i++;
    }
    return a[i] == b[i];
}

void shell_init() {
    shell_ptr = 0;
    for(int i = 0; i < SHELL_BUFFER_SIZE; i++) shell_buffer[i] = 0;
    kprint("\nQuillOS v0.1\n> ");
}

void process_command(char* input) {
    char cmd[64];
    char arg[64];

    // Zero out local buffers
    for(int x = 0; x < 64; x++) {
        cmd[x] = 0;
        arg[x] = 0;
    }

    int i = 0;
    while (input[i] == ' ' && input[i] != '\0') i++;

    int c_idx = 0;
    while (input[i] != ' ' && input[i] != '\0' && c_idx < 63) {
        cmd[c_idx++] = input[i++];
    }
    cmd[c_idx] = '\0';

    while (input[i] == ' ' && input[i] != '\0') i++;

    int a_idx = 0;
    while (input[i] != '\0' && a_idx < 63) {
        arg[a_idx++] = input[i++];
    }
    arg[a_idx] = '\0';

    if (safe_strlen(cmd) == 0) return;

    if (safe_compare(cmd, "help")) {
        kprint("\nCommands: help, cls, ver, halt, reboot, color");
    }
    else if (safe_compare(cmd, "cls")) {
        console_clear();
        shell_init(); 
    }
    else if (safe_compare(cmd, "ver")) {
        kprint("\nQuillOS v0.1");
    }
    else if (safe_compare(cmd, "uptime")) {
        char time_buf[32];
        itoa(ticks / 1000, time_buf); // Convert ms to seconds
        kprint("\nUptime: ");
        kprint(time_buf);
        kprint(" seconds.");
    }
    else if (safe_compare(cmd, "color")) {
        if (safe_compare(arg, "red")) set_bg_color(0xFF0000);
        else if (safe_compare(arg, "blue")) set_bg_color(0x0000FF);
        else if (safe_compare(arg, "green")) set_bg_color(0x00FF00);
        else if (safe_compare(arg, "dark")) set_bg_color(0x111111); 
        else kprint("\nTry: red, blue, green, or dark");
        
        console_clear();
        kprint("\nBackground updated.");
    }
    else if (safe_compare(cmd, "reboot")) {
        kprint("\nRebooting...");
        // Pulse Keyboard Controller
        for (int j = 0; j < 100; j++) {
            outb(0x64, 0xFE);
        }
        
        // Force Triple Fault fallback
        struct {
            uint16_t limit;
            uint64_t base;
        } __attribute__((packed)) invalid_idtr = {0, 0};
        
        asm volatile("lidt %0; int3" :: "m"(invalid_idtr));
        return;
    }
    else if (safe_compare(cmd, "halt")) {
        kprint("\nHalting...");
        outb(0xf4, 0x10);
        asm volatile("cli; hlt");
    }
    else {
        kprint("\nUnknown command.");
    }
}

void shell_update(char c) {
    if (c == '\n') {
        shell_buffer[shell_ptr] = '\0';
        process_command(shell_buffer);
        shell_ptr = 0;
        for(int i = 0; i < SHELL_BUFFER_SIZE; i++) shell_buffer[i] = 0;
        kprint("\n> ");
    } else if (c == '\b') {
        if (shell_ptr > 0) {
            shell_ptr--;
            shell_buffer[shell_ptr] = '\0';
            console_backspace();
        }
    } else {
        if (shell_ptr < SHELL_BUFFER_SIZE - 1) {
            shell_buffer[shell_ptr++] = c;
            console_putc(c);
        }
    }
}

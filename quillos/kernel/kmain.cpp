#include "idt.h"
#include "shell.h"

extern "C" void kmain(void) {
    // ... your existing initialization (framebuffer, GDT, etc) ...
    
    idt_init();     // Sets up IDT and remaps PIC
    shell_init();   // <--- Add this here! Prints the first "QuillOS >"

    // Your kernel should now wait for interrupts
    while(1) {
        asm volatile ("hlt"); 
    }
}

#include "idt.h"
#include "io.h"
#include <stddef.h>

extern "C" void load_idt(void*);
extern "C" void keyboard_handler_stub();
extern "C" void dummy_handler();

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDTPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

IDTEntry idt[256];
IDTPointer idt_ptr;

void set_idt_entry(int num, void (*handler)()) {
    uint64_t addr = (uint64_t)handler;

    idt[num].offset_low = addr & 0xFFFF;
    idt[num].selector = 0x28; 
    idt[num].ist = 0;
    idt[num].type_attr = 0x8E; 
    idt[num].offset_mid = (addr >> 16) & 0xFFFF;
    idt[num].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[num].zero = 0;
}

void pic_remap() {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();

    outb(0x21, 0x20); io_wait(); 
    outb(0xA1, 0x28); io_wait(); 

    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();

    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    // Mask all except Keyboard (IRQ1)
    outb(0x21, 0xFD);
    outb(0xA1, 0xFF);
}

void idt_init() {
    // Fill IDT with dummy handlers to prevent triple faults
    for(int i = 0; i < 256; i++) {
        set_idt_entry(i, dummy_handler);
    }

    pic_remap();

    // Specific handler for Keyboard
    set_idt_entry(33, keyboard_handler_stub);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    load_idt(&idt_ptr);

    // Enable interrupts
    asm volatile ("sti");
}

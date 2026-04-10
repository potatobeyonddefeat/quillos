#include "gdt.h"
#include <stddef.h>

extern void console_print(const char* str);

namespace GDT {

    // ------------------------------------------------------------
    // Descriptor structures
    // ------------------------------------------------------------

    struct Entry {
        uint16_t limit_low;
        uint16_t base_low;
        uint8_t  base_mid;
        uint8_t  access;
        uint8_t  granularity;
        uint8_t  base_high;
    } __attribute__((packed));

    // A 64-bit TSS descriptor occupies two consecutive 8-byte slots
    struct TSSDescriptor {
        uint16_t limit_low;
        uint16_t base_low;
        uint8_t  base_mid;
        uint8_t  access;
        uint8_t  granularity;
        uint8_t  base_high;
        uint32_t base_upper;
        uint32_t reserved;
    } __attribute__((packed));

    struct TSS {
        uint32_t reserved0;
        uint64_t rsp0;
        uint64_t rsp1;
        uint64_t rsp2;
        uint64_t reserved1;
        uint64_t ist1;
        uint64_t ist2;
        uint64_t ist3;
        uint64_t ist4;
        uint64_t ist5;
        uint64_t ist6;
        uint64_t ist7;
        uint64_t reserved2;
        uint16_t reserved3;
        uint16_t iomap_base;
    } __attribute__((packed));

    struct GDTPointer {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed));

    // GDT has 11 slots: 0..4 null pad, 5 kCode, 6 kData, 7 uCode,
    // 8 uData, 9/10 TSS. Total 11 * 8 = 88 bytes.
    static Entry       gdt_entries[11] __attribute__((aligned(16)));
    static TSS         tss            __attribute__((aligned(16)));
    static GDTPointer  gdt_ptr;

    // ------------------------------------------------------------
    // Assembly helpers (interrupts.asm)
    // ------------------------------------------------------------

    extern "C" void gdt_flush(void* ptr, uint64_t cs, uint64_t ds);
    extern "C" void tss_flush(uint16_t sel);

    // ------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------

    static void set_entry(int i, uint8_t access, uint8_t granularity) {
        gdt_entries[i].limit_low   = 0;
        gdt_entries[i].base_low    = 0;
        gdt_entries[i].base_mid    = 0;
        gdt_entries[i].access      = access;
        gdt_entries[i].granularity = granularity;
        gdt_entries[i].base_high   = 0;
    }

    static void set_tss_entry(int i, uint64_t base, uint32_t limit) {
        TSSDescriptor* td = (TSSDescriptor*)&gdt_entries[i];
        td->limit_low   = (uint16_t)(limit & 0xFFFF);
        td->base_low    = (uint16_t)(base & 0xFFFF);
        td->base_mid    = (uint8_t)((base >> 16) & 0xFF);
        td->access      = 0x89;  // P=1, DPL=0, type=9 (avail 64-bit TSS)
        td->granularity = (uint8_t)((limit >> 16) & 0x0F);  // G=0
        td->base_high   = (uint8_t)((base >> 24) & 0xFF);
        td->base_upper  = (uint32_t)((base >> 32) & 0xFFFFFFFF);
        td->reserved    = 0;
    }

    // ------------------------------------------------------------
    // init
    // ------------------------------------------------------------

    void init() {
        // Zero everything
        uint8_t* p = (uint8_t*)gdt_entries;
        for (size_t i = 0; i < sizeof(gdt_entries); i++) p[i] = 0;

        // Slot index 5 -> byte offset 0x28 (kernel CS)
        // Access 0x9A: P=1, DPL=0, S=1, type=code/r/a
        // Granularity 0xA0: G=0, D=0, L=1 (long mode), AVL=0
        set_entry(5, 0x9A, 0xA0);

        // Slot index 6 -> 0x30 (kernel DS)
        // Access 0x92: P=1, DPL=0, S=1, type=data/rw
        set_entry(6, 0x92, 0xA0);

        // Slot index 7 -> 0x38 (user CS)
        // Access 0xFA: P=1, DPL=3, S=1, type=code/r/a
        set_entry(7, 0xFA, 0xA0);

        // Slot index 8 -> 0x40 (user DS)
        // Access 0xF2: P=1, DPL=3, S=1, type=data/rw
        set_entry(8, 0xF2, 0xA0);

        // Slots 9/10 -> 0x48 TSS descriptor
        uint8_t* tss_bytes = (uint8_t*)&tss;
        for (size_t i = 0; i < sizeof(TSS); i++) tss_bytes[i] = 0;
        tss.iomap_base = sizeof(TSS);  // No I/O bitmap
        set_tss_entry(9, (uint64_t)&tss, sizeof(TSS) - 1);

        // Load
        gdt_ptr.limit = sizeof(gdt_entries) - 1;
        gdt_ptr.base  = (uint64_t)&gdt_entries;
        gdt_flush(&gdt_ptr, KERNEL_CS, KERNEL_DS);
        tss_flush(TSS_SEL);

        console_print("\n[GDT] user CS=0x3B DS=0x43, TSS loaded");
    }

    void set_kernel_stack(uint64_t rsp) {
        tss.rsp0 = rsp;
    }

} // namespace GDT

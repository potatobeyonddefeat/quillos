#pragma once
#include <stdint.h>

namespace CPU {
    struct Info {
        char vendor[13];       // e.g., "GenuineIntel"
        char brand[49];        // e.g., "Intel(R) Core(TM) i5-..."
        uint32_t family;
        uint32_t model;
        uint32_t stepping;
        bool has_sse;
        bool has_sse2;
        bool has_fpu;
        bool has_apic;
        bool has_mmx;
        bool has_pae;
        bool has_x2apic;
        bool has_hypervisor;
    };

    void init();
    const Info* get_info();
}

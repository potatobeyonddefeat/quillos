#include "cpu.h"

extern void console_print(const char* str);

namespace CPU {

    static Info info;

    static inline void cpuid(uint32_t leaf,
                             uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
        asm volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0));
    }

    static void copy_reg(char* dst, uint32_t reg) {
        dst[0] = (char)(reg & 0xFF);
        dst[1] = (char)((reg >> 8) & 0xFF);
        dst[2] = (char)((reg >> 16) & 0xFF);
        dst[3] = (char)((reg >> 24) & 0xFF);
    }

    void init() {
        // Zero everything
        for (int i = 0; i < 13; i++) info.vendor[i] = 0;
        for (int i = 0; i < 49; i++) info.brand[i] = 0;

        uint32_t a, b, c, d;

        // CPUID 0x00 — vendor string in EBX:EDX:ECX
        cpuid(0, &a, &b, &c, &d);
        copy_reg(&info.vendor[0], b);
        copy_reg(&info.vendor[4], d);
        copy_reg(&info.vendor[8], c);
        info.vendor[12] = '\0';

        // CPUID 0x01 — family/model/stepping + features
        cpuid(1, &a, &b, &c, &d);
        info.stepping = a & 0xF;
        info.model    = (a >> 4) & 0xF;
        info.family   = (a >> 8) & 0xF;
        if (info.family == 0xF) {
            info.family += (a >> 20) & 0xFF;
        }
        if (info.family == 0x6 || info.family == 0xF) {
            info.model |= ((a >> 16) & 0xF) << 4;
        }

        info.has_fpu       = (d >> 0)  & 1;
        info.has_pae       = (d >> 6)  & 1;
        info.has_apic      = (d >> 9)  & 1;
        info.has_mmx       = (d >> 23) & 1;
        info.has_sse       = (d >> 25) & 1;
        info.has_sse2      = (d >> 26) & 1;
        info.has_x2apic    = (c >> 21) & 1;
        info.has_hypervisor= (c >> 31) & 1;

        // Brand string: CPUID 0x80000002, 0x80000003, 0x80000004
        cpuid(0x80000000, &a, &b, &c, &d);
        if (a >= 0x80000004) {
            for (int leaf = 0; leaf < 3; leaf++) {
                cpuid(0x80000002 + leaf, &a, &b, &c, &d);
                int off = leaf * 16;
                copy_reg(&info.brand[off +  0], a);
                copy_reg(&info.brand[off +  4], b);
                copy_reg(&info.brand[off +  8], c);
                copy_reg(&info.brand[off + 12], d);
            }
            info.brand[48] = '\0';
            // Trim leading spaces
            int start = 0;
            while (info.brand[start] == ' ') start++;
            if (start > 0) {
                int j = 0;
                while (info.brand[start + j]) {
                    info.brand[j] = info.brand[start + j];
                    j++;
                }
                info.brand[j] = '\0';
            }
        } else {
            const char* fallback = "Unknown CPU";
            int i = 0;
            while (fallback[i] && i < 48) { info.brand[i] = fallback[i]; i++; }
            info.brand[i] = '\0';
        }

        console_print("\n[CPU] ");
        console_print(info.vendor);
        console_print(" — ");
        console_print(info.brand);
    }

    const Info* get_info() { return &info; }
}

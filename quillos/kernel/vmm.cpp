#include "vmm.h"
#include "memory.h"
#include <stddef.h>

extern void console_print(const char* str);

namespace VMM {

    static constexpr uint64_t PAGE_SIZE = 4096;
    static constexpr uint64_t ADDR_MASK = ~0xFFFULL;

    static uint64_t kernel_pml4_phys = 0;

    uint64_t kernel_cr3() { return kernel_pml4_phys; }

    bool init() {
        uint64_t cr3;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        kernel_pml4_phys = cr3 & ADDR_MASK;
        console_print("\n[VMM] kernel PML4 captured");
        return true;
    }

    // --------------------------------------------------------------
    // Table walking
    // --------------------------------------------------------------

    static inline uint64_t* pt_virt(uint64_t phys) {
        return (uint64_t*)Memory::phys_to_virt(phys);
    }

    static uint64_t* get_or_alloc_table(uint64_t* parent, uint64_t idx, bool user) {
        uint64_t entry = parent[idx];
        if (entry & PAGE_PRESENT) {
            return pt_virt(entry & ADDR_MASK);
        }
        uint64_t new_phys = Memory::pmm_alloc_page();
        if (!new_phys) return nullptr;
        // Zero fresh table (pmm already zeros pages)
        uint64_t flags = PAGE_PRESENT | PAGE_WRITE;
        if (user) flags |= PAGE_USER;
        parent[idx] = new_phys | flags;
        return pt_virt(new_phys);
    }

    bool map_page(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags) {
        uint64_t pml4_i = (virt >> 39) & 0x1FF;
        uint64_t pdpt_i = (virt >> 30) & 0x1FF;
        uint64_t pd_i   = (virt >> 21) & 0x1FF;
        uint64_t pt_i   = (virt >> 12) & 0x1FF;

        bool user = (flags & PAGE_USER) != 0;

        uint64_t* pml4 = pt_virt(cr3);
        uint64_t* pdpt = get_or_alloc_table(pml4, pml4_i, user);
        if (!pdpt) return false;
        uint64_t* pd = get_or_alloc_table(pdpt, pdpt_i, user);
        if (!pd) return false;
        uint64_t* pt = get_or_alloc_table(pd, pd_i, user);
        if (!pt) return false;

        pt[pt_i] = (phys & ADDR_MASK) | (flags & 0xFFF) | PAGE_PRESENT;

        // Invalidate TLB for this page (only effective on current CR3,
        // but that is the common case).
        asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
        return true;
    }

    void unmap_page(uint64_t cr3, uint64_t virt) {
        uint64_t pml4_i = (virt >> 39) & 0x1FF;
        uint64_t pdpt_i = (virt >> 30) & 0x1FF;
        uint64_t pd_i   = (virt >> 21) & 0x1FF;
        uint64_t pt_i   = (virt >> 12) & 0x1FF;

        uint64_t* pml4 = pt_virt(cr3);
        if (!(pml4[pml4_i] & PAGE_PRESENT)) return;
        uint64_t* pdpt = pt_virt(pml4[pml4_i] & ADDR_MASK);
        if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return;
        uint64_t* pd = pt_virt(pdpt[pdpt_i] & ADDR_MASK);
        if (!(pd[pd_i] & PAGE_PRESENT)) return;
        uint64_t* pt = pt_virt(pd[pd_i] & ADDR_MASK);
        if (!(pt[pt_i] & PAGE_PRESENT)) return;

        // Free the backing page only if it's a user page we own
        uint64_t entry = pt[pt_i];
        if (entry & PAGE_USER) {
            Memory::pmm_free_page(entry & ADDR_MASK);
        }
        pt[pt_i] = 0;
        asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
    }

    uint64_t translate(uint64_t cr3, uint64_t virt) {
        uint64_t pml4_i = (virt >> 39) & 0x1FF;
        uint64_t pdpt_i = (virt >> 30) & 0x1FF;
        uint64_t pd_i   = (virt >> 21) & 0x1FF;
        uint64_t pt_i   = (virt >> 12) & 0x1FF;
        uint64_t off    = virt & 0xFFF;

        uint64_t* pml4 = pt_virt(cr3);
        if (!(pml4[pml4_i] & PAGE_PRESENT)) return 0;
        uint64_t* pdpt = pt_virt(pml4[pml4_i] & ADDR_MASK);
        if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return 0;
        uint64_t* pd = pt_virt(pdpt[pdpt_i] & ADDR_MASK);
        if (!(pd[pd_i] & PAGE_PRESENT)) return 0;
        uint64_t* pt = pt_virt(pd[pd_i] & ADDR_MASK);
        if (!(pt[pt_i] & PAGE_PRESENT)) return 0;
        return (pt[pt_i] & ADDR_MASK) | off;
    }

    // --------------------------------------------------------------
    // Address space lifecycle
    // --------------------------------------------------------------

    uint64_t create_address_space() {
        uint64_t new_phys = Memory::pmm_alloc_page();
        if (!new_phys) return 0;

        uint64_t* dst = pt_virt(new_phys);
        uint64_t* src = pt_virt(kernel_pml4_phys);

        // Lower half: fresh user pages
        for (int i = 0; i < 256; i++) dst[i] = 0;
        // Upper half: share kernel mappings
        for (int i = 256; i < 512; i++) dst[i] = src[i];

        return new_phys;
    }

    // Recursively free user pages + intermediate tables in the
    // lower half of `cr3`. Upper half is shared with the kernel
    // and must never be touched.
    static void free_pt(uint64_t pt_phys) {
        uint64_t* pt = pt_virt(pt_phys);
        for (int i = 0; i < 512; i++) {
            uint64_t e = pt[i];
            if ((e & PAGE_PRESENT) && (e & PAGE_USER)) {
                Memory::pmm_free_page(e & ADDR_MASK);
            }
        }
        Memory::pmm_free_page(pt_phys);
    }

    static void free_pd(uint64_t pd_phys) {
        uint64_t* pd = pt_virt(pd_phys);
        for (int i = 0; i < 512; i++) {
            uint64_t e = pd[i];
            if ((e & PAGE_PRESENT) && (e & PAGE_USER)) {
                free_pt(e & ADDR_MASK);
            }
        }
        Memory::pmm_free_page(pd_phys);
    }

    static void free_pdpt(uint64_t pdpt_phys) {
        uint64_t* pdpt = pt_virt(pdpt_phys);
        for (int i = 0; i < 512; i++) {
            uint64_t e = pdpt[i];
            if ((e & PAGE_PRESENT) && (e & PAGE_USER)) {
                free_pd(e & ADDR_MASK);
            }
        }
        Memory::pmm_free_page(pdpt_phys);
    }

    void destroy_address_space(uint64_t cr3) {
        if (!cr3 || cr3 == kernel_pml4_phys) return;
        uint64_t* pml4 = pt_virt(cr3);
        // Walk only the user (lower) half
        for (int i = 0; i < 256; i++) {
            uint64_t e = pml4[i];
            if ((e & PAGE_PRESENT) && (e & PAGE_USER)) {
                free_pdpt(e & ADDR_MASK);
            }
            pml4[i] = 0;
        }
        Memory::pmm_free_page(cr3);
    }

    // --------------------------------------------------------------
    // alloc_user_page / clone / copy helpers
    // --------------------------------------------------------------

    bool alloc_user_page(uint64_t cr3, uint64_t virt, uint64_t flags) {
        uint64_t phys = Memory::pmm_alloc_page();
        if (!phys) return false;
        if (!map_page(cr3, virt, phys, flags | PAGE_USER | PAGE_PRESENT)) {
            Memory::pmm_free_page(phys);
            return false;
        }
        return true;
    }

    static void page_copy(uint64_t dst_phys, uint64_t src_phys) {
        uint8_t* d = (uint8_t*)Memory::phys_to_virt(dst_phys);
        uint8_t* s = (uint8_t*)Memory::phys_to_virt(src_phys);
        for (uint64_t i = 0; i < PAGE_SIZE; i++) d[i] = s[i];
    }

    uint64_t clone_address_space(uint64_t src_cr3) {
        uint64_t dst_cr3 = create_address_space();
        if (!dst_cr3) return 0;

        uint64_t* src_pml4 = pt_virt(src_cr3);
        // Walk the user half only
        for (int i4 = 0; i4 < 256; i4++) {
            uint64_t e4 = src_pml4[i4];
            if (!(e4 & PAGE_PRESENT)) continue;
            uint64_t* src_pdpt = pt_virt(e4 & ADDR_MASK);
            for (int i3 = 0; i3 < 512; i3++) {
                uint64_t e3 = src_pdpt[i3];
                if (!(e3 & PAGE_PRESENT)) continue;
                uint64_t* src_pd = pt_virt(e3 & ADDR_MASK);
                for (int i2 = 0; i2 < 512; i2++) {
                    uint64_t e2 = src_pd[i2];
                    if (!(e2 & PAGE_PRESENT)) continue;
                    uint64_t* src_pt = pt_virt(e2 & ADDR_MASK);
                    for (int i1 = 0; i1 < 512; i1++) {
                        uint64_t e1 = src_pt[i1];
                        if (!(e1 & PAGE_PRESENT)) continue;
                        if (!(e1 & PAGE_USER)) continue;  // kernel-only, skip

                        uint64_t virt =
                            ((uint64_t)i4 << 39) |
                            ((uint64_t)i3 << 30) |
                            ((uint64_t)i2 << 21) |
                            ((uint64_t)i1 << 12);

                        uint64_t new_phys = Memory::pmm_alloc_page();
                        if (!new_phys) {
                            destroy_address_space(dst_cr3);
                            return 0;
                        }
                        page_copy(new_phys, e1 & ADDR_MASK);
                        if (!map_page(dst_cr3, virt, new_phys, e1 & 0xFFF)) {
                            Memory::pmm_free_page(new_phys);
                            destroy_address_space(dst_cr3);
                            return 0;
                        }
                    }
                }
            }
        }
        return dst_cr3;
    }

    bool copy_to_user(uint64_t cr3, uint64_t user_virt,
                      const void* src, uint64_t len) {
        const uint8_t* s = (const uint8_t*)src;
        uint64_t remaining = len;
        uint64_t v = user_virt;

        while (remaining > 0) {
            uint64_t page_off = v & 0xFFF;
            uint64_t chunk = PAGE_SIZE - page_off;
            if (chunk > remaining) chunk = remaining;

            uint64_t phys = translate(cr3, v);
            if (!phys) return false;
            uint8_t* dst = (uint8_t*)Memory::phys_to_virt(phys & ADDR_MASK) + page_off;
            for (uint64_t i = 0; i < chunk; i++) dst[i] = s[i];

            s += chunk;
            v += chunk;
            remaining -= chunk;
        }
        return true;
    }

    bool zero_user(uint64_t cr3, uint64_t user_virt, uint64_t len) {
        uint64_t v = user_virt;
        uint64_t remaining = len;
        while (remaining > 0) {
            uint64_t page_off = v & 0xFFF;
            uint64_t chunk = PAGE_SIZE - page_off;
            if (chunk > remaining) chunk = remaining;
            uint64_t phys = translate(cr3, v);
            if (!phys) return false;
            uint8_t* dst = (uint8_t*)Memory::phys_to_virt(phys & ADDR_MASK) + page_off;
            for (uint64_t i = 0; i < chunk; i++) dst[i] = 0;
            v += chunk;
            remaining -= chunk;
        }
        return true;
    }

    void switch_to(uint64_t cr3) {
        asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
    }

} // namespace VMM

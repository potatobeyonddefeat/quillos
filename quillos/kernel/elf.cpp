#include "elf.h"
#include "vmm.h"
#include "memory.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace ELF {

    // ------------------------------------------------------------
    // ELF64 structures (subset)
    // ------------------------------------------------------------

    static constexpr uint8_t  ELFMAG0 = 0x7F;
    static constexpr uint8_t  ELFMAG1 = 'E';
    static constexpr uint8_t  ELFMAG2 = 'L';
    static constexpr uint8_t  ELFMAG3 = 'F';
    static constexpr uint8_t  ELFCLASS64 = 2;
    static constexpr uint8_t  ELFDATA2LSB = 1;
    static constexpr uint16_t EM_X86_64 = 62;
    static constexpr uint16_t ET_EXEC = 2;
    static constexpr uint16_t ET_DYN  = 3;
    static constexpr uint32_t PT_LOAD = 1;
    static constexpr uint32_t PF_X = 1;
    static constexpr uint32_t PF_W = 2;
    static constexpr uint32_t PF_R = 4;

    struct Ehdr {
        uint8_t  e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint64_t e_entry;
        uint64_t e_phoff;
        uint64_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    } __attribute__((packed));

    struct Phdr {
        uint32_t p_type;
        uint32_t p_flags;
        uint64_t p_offset;
        uint64_t p_vaddr;
        uint64_t p_paddr;
        uint64_t p_filesz;
        uint64_t p_memsz;
        uint64_t p_align;
    } __attribute__((packed));

    // ------------------------------------------------------------

    bool is_elf64(const void* image, uint64_t len) {
        if (len < sizeof(Ehdr)) return false;
        const Ehdr* eh = (const Ehdr*)image;
        if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
            eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3) return false;
        if (eh->e_ident[4] != ELFCLASS64) return false;
        if (eh->e_ident[5] != ELFDATA2LSB) return false;
        if (eh->e_machine != EM_X86_64) return false;
        if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return false;
        return true;
    }

    // Ensure every page in [virt, virt+len) is allocated+mapped
    // in `cr3` with USER|PRESENT (plus extra flags). Skips pages
    // that are already mapped.
    static bool ensure_mapped(uint64_t cr3, uint64_t virt,
                              uint64_t len, uint64_t flags) {
        uint64_t start = virt & ~0xFFFULL;
        uint64_t end   = (virt + len + 0xFFF) & ~0xFFFULL;
        for (uint64_t v = start; v < end; v += 4096) {
            if (VMM::translate(cr3, v)) continue;
            if (!VMM::alloc_user_page(cr3, v, flags)) return false;
        }
        return true;
    }

    LoadResult load(uint64_t cr3, const void* image, uint64_t len) {
        LoadResult r = { false, 0, 0 };
        if (!is_elf64(image, len)) {
            console_print("\n[ELF] not an ELF64 image");
            return r;
        }
        const uint8_t* base = (const uint8_t*)image;
        const Ehdr* eh = (const Ehdr*)image;

        if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > len) {
            console_print("\n[ELF] truncated program headers");
            return r;
        }

        uint64_t highest = 0;

        for (uint16_t i = 0; i < eh->e_phnum; i++) {
            const Phdr* ph = (const Phdr*)(base + eh->e_phoff + i * eh->e_phentsize);
            if (ph->p_type != PT_LOAD) continue;
            if (ph->p_memsz == 0) continue;

            // Flags: writable segments get W. All user pages get U.
            uint64_t flags = VMM::PAGE_USER | VMM::PAGE_PRESENT;
            if (ph->p_flags & PF_W) flags |= VMM::PAGE_WRITE;

            // Allocate pages covering [p_vaddr, p_vaddr + p_memsz).
            if (!ensure_mapped(cr3, ph->p_vaddr, ph->p_memsz, flags)) {
                console_print("\n[ELF] alloc_user_page failed");
                return r;
            }

            // Zero the whole region first (handles p_memsz > p_filesz
            // for .bss-style segments).
            if (!VMM::zero_user(cr3, ph->p_vaddr, ph->p_memsz)) return r;

            // Copy file contents into place.
            if (ph->p_filesz > 0) {
                if (ph->p_offset + ph->p_filesz > len) {
                    console_print("\n[ELF] segment file range oob");
                    return r;
                }
                if (!VMM::copy_to_user(cr3, ph->p_vaddr,
                                       base + ph->p_offset, ph->p_filesz)) {
                    return r;
                }
            }

            uint64_t seg_top = ph->p_vaddr + ph->p_memsz;
            if (seg_top > highest) highest = seg_top;
        }

        r.ok = true;
        r.entry = eh->e_entry;
        r.highest_va = highest;
        return r;
    }

} // namespace ELF

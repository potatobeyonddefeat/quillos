#pragma once
#include <stdint.h>

// ================================================================
// Minimal ELF64 loader
//
// Loads PT_LOAD segments from an in-memory ELF image into a
// freshly-prepared user address space. Enough to run small
// static programs built with `x86_64-elf-gcc -static -nostdlib`.
// ================================================================

namespace ELF {

    struct LoadResult {
        bool     ok;
        uint64_t entry;        // Entry point VA
        uint64_t highest_va;   // Highest mapped byte (for heap placement)
    };

    // Parse + load. Allocates user pages in `cr3` via VMM.
    // On failure, leaves partial mappings -- caller should tear
    // down the address space.
    LoadResult load(uint64_t cr3, const void* image, uint64_t len);

    // Quick sanity check: is this blob a 64-bit little-endian
    // executable ELF?
    bool is_elf64(const void* image, uint64_t len);

} // namespace ELF

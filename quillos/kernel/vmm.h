#pragma once
#include <stdint.h>

// ================================================================
// Virtual Memory Manager
//
// Builds on top of Memory::pmm_* to provide per-process page
// tables. Each user process gets its own PML4 whose upper half
// is cloned from the kernel PML4 (HHDM, kernel code/data, heap).
// The lower half is private user space.
// ================================================================

namespace VMM {

    static constexpr uint64_t PAGE_PRESENT = 1ULL << 0;
    static constexpr uint64_t PAGE_WRITE   = 1ULL << 1;
    static constexpr uint64_t PAGE_USER    = 1ULL << 2;
    static constexpr uint64_t PAGE_PWT     = 1ULL << 3;
    static constexpr uint64_t PAGE_PCD     = 1ULL << 4;
    static constexpr uint64_t PAGE_ACCESS  = 1ULL << 5;
    static constexpr uint64_t PAGE_DIRTY   = 1ULL << 6;
    static constexpr uint64_t PAGE_NX      = 1ULL << 63;

    bool init();

    // The kernel's own PML4 (set up by Limine at boot)
    uint64_t kernel_cr3();

    // Create a fresh address space whose upper half (kernel) is
    // shared with the kernel PML4. Returns the physical address
    // of the new PML4, or 0 on failure.
    uint64_t create_address_space();

    // Tear down an address space created by create_address_space.
    // Frees the user (lower-half) pages and intermediate tables.
    // Does NOT switch away from it -- the caller must ensure a
    // different CR3 is live first.
    void destroy_address_space(uint64_t cr3);

    // Clone all lower-half pages in `src_cr3` into a new address
    // space (eager copy). The kernel upper half is shared. Used
    // by fork().
    uint64_t clone_address_space(uint64_t src_cr3);

    // Map/unmap a single 4 KiB page.
    bool map_page(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
    void unmap_page(uint64_t cr3, uint64_t virt);

    // Allocate a fresh physical page and map it in the given
    // address space. Returns false on OOM.
    bool alloc_user_page(uint64_t cr3, uint64_t virt, uint64_t flags);

    // Walk the tables and return the physical address backing
    // `virt`, or 0 if unmapped.
    uint64_t translate(uint64_t cr3, uint64_t virt);

    // Load a new CR3.
    void switch_to(uint64_t cr3);

    // Copy from kernel memory into a user address space. Used by
    // the ELF loader to place segments after allocating backing
    // pages. Crosses page boundaries automatically.
    bool copy_to_user(uint64_t cr3, uint64_t user_virt,
                      const void* src, uint64_t len);

    // Zero a region in a user address space. Pages must already
    // be mapped.
    bool zero_user(uint64_t cr3, uint64_t user_virt, uint64_t len);

} // namespace VMM

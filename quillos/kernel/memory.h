#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Memory {

    static constexpr uint64_t PAGE_SIZE = 4096;

    // ================================================================
    // Top-level init (call once during boot)
    // ================================================================
    bool init();

    // ================================================================
    // Address conversion (uses Limine HHDM)
    // ================================================================
    void*    phys_to_virt(uint64_t phys);
    uint64_t virt_to_phys(void* virt);

    // ================================================================
    // Physical Memory Manager
    //
    // Bitmap-based page frame allocator. Tracks every 4KB page of
    // physical RAM across all usable regions reported by Limine.
    // ================================================================
    uint64_t pmm_alloc_page();               // Returns phys addr (0 on fail)
    void     pmm_free_page(uint64_t phys);
    uint64_t pmm_total_pages();
    uint64_t pmm_used_pages();
    uint64_t pmm_free_pages();
    uint64_t pmm_total_bytes();

    // ================================================================
    // Kernel Heap Allocator
    //
    // Free-list allocator backed by PMM pages.
    // Supports split on alloc, coalesce on free, dynamic growth.
    // ================================================================
    void*    kmalloc(uint64_t size);
    void     kfree(void* ptr);
    void*    kcalloc(uint64_t count, uint64_t size);  // Zero-filled

    // Heap diagnostics
    uint64_t heap_total_size();         // Committed heap bytes
    uint64_t heap_allocated_bytes();    // Bytes in use
    uint64_t heap_free_bytes();         // Bytes available (in free blocks)
    uint64_t heap_alloc_count();        // Number of live allocations
    uint64_t heap_block_count();        // Total blocks (free + used)
    uint64_t heap_largest_free();       // Largest single free block

    // Compatibility shims (old API)
    uint64_t get_total_pages();
    uint64_t get_used_pages();
    uint64_t get_free_pages();
    uint64_t get_heap_used();
}

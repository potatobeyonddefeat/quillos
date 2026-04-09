#include "memory.h"
#include <limine.h>

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Memory {

    static constexpr uint64_t PAGE_SIZE = 4096;
    static constexpr uint64_t MAX_PAGES = 32768; // Track up to 128MB

    static volatile struct limine_memmap_request memmap_req = {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    static volatile struct limine_hhdm_request hhdm_req = {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    static uint8_t page_bitmap[MAX_PAGES / 8];
    static uint64_t total_pages = 0;
    static uint64_t used_pages = 0;
    static uint64_t mem_base = 0;
    static uint64_t hhdm_offset = 0;

    // Simple 64KB kernel heap (bump allocator)
    static uint8_t heap[65536];
    static uint64_t heap_offset = 0;

    bool init() {
        // Zero bitmap
        for (uint64_t i = 0; i < sizeof(page_bitmap); i++)
            page_bitmap[i] = 0;

        if (hhdm_req.response)
            hhdm_offset = hhdm_req.response->offset;

        if (!memmap_req.response) {
            console_print("\n[MEM] No memory map from bootloader");
            return false;
        }

        // Find the largest usable memory region
        uint64_t best_base = 0;
        uint64_t best_size = 0;
        uint64_t total_usable = 0;

        for (uint64_t i = 0; i < memmap_req.response->entry_count; i++) {
            struct limine_memmap_entry *entry = memmap_req.response->entries[i];
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                total_usable += entry->length;
                if (entry->length > best_size) {
                    best_base = entry->base;
                    best_size = entry->length;
                }
            }
        }

        if (best_size == 0) {
            console_print("\n[MEM] No usable memory found");
            return false;
        }

        mem_base = best_base;
        total_pages = best_size / PAGE_SIZE;
        if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;
        used_pages = 0;

        char buf[32];
        console_print("\n[MEM] ");
        itoa(total_pages * 4, buf);
        console_print(buf);
        console_print("KB available (");
        itoa(total_pages, buf);
        console_print(buf);
        console_print(" pages)");

        return true;
    }

    void* alloc_page() {
        for (uint64_t i = 0; i < total_pages; i++) {
            uint64_t byte_idx = i / 8;
            uint64_t bit_idx = i % 8;
            if (!(page_bitmap[byte_idx] & (1 << bit_idx))) {
                page_bitmap[byte_idx] |= (1 << bit_idx);
                used_pages++;
                uint64_t phys = mem_base + i * PAGE_SIZE;
                return (void*)(hhdm_offset + phys);
            }
        }
        return nullptr;
    }

    void free_page(void* addr) {
        uint64_t phys = (uint64_t)addr - hhdm_offset;
        if (phys < mem_base) return;
        uint64_t page = (phys - mem_base) / PAGE_SIZE;
        if (page >= total_pages) return;

        uint64_t byte_idx = page / 8;
        uint64_t bit_idx = page % 8;
        if (page_bitmap[byte_idx] & (1 << bit_idx)) {
            page_bitmap[byte_idx] &= ~(1 << bit_idx);
            used_pages--;
        }
    }

    void* kmalloc(uint64_t size) {
        size = (size + 7) & ~7ULL; // Align to 8 bytes
        if (heap_offset + size > sizeof(heap)) return nullptr;
        void* ptr = &heap[heap_offset];
        heap_offset += size;
        return ptr;
    }

    void kfree(void*) {
        // Bump allocator — no individual free support
    }

    uint64_t get_total_pages() { return total_pages; }
    uint64_t get_used_pages() { return used_pages; }
    uint64_t get_free_pages() { return total_pages - used_pages; }
    uint64_t get_heap_used() { return heap_offset; }
}

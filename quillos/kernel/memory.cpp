#include "memory.h"
#include <limine.h>

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Memory {

    // ================================================================
    // Limine requests
    // ================================================================

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

    // ================================================================
    // HHDM offset (set during init)
    // ================================================================

    static uint64_t hhdm = 0;

    void* phys_to_virt(uint64_t phys) {
        return (void*)(hhdm + phys);
    }

    uint64_t virt_to_phys(void* virt) {
        return (uint64_t)virt - hhdm;
    }

    // ================================================================
    // Physical Memory Manager — Bitmap
    //
    // One bit per physical page (4KB).
    // Bit = 1 means USED, bit = 0 means FREE.
    // Bitmap covers the entire physical address space up to the
    // highest usable address reported by Limine.
    // ================================================================

    static uint8_t* pmm_bitmap = nullptr;
    static uint64_t pmm_bitmap_bytes = 0;
    static uint64_t pmm_highest_page = 0;
    static uint64_t pmm_total = 0;       // Total usable pages
    static uint64_t pmm_used = 0;        // Currently used pages
    static uint64_t pmm_search_hint = 0; // Start next search here

    static inline void bmp_set(uint64_t page) {
        pmm_bitmap[page / 8] |= (uint8_t)(1 << (page % 8));
    }

    static inline void bmp_clear(uint64_t page) {
        pmm_bitmap[page / 8] &= (uint8_t)~(1 << (page % 8));
    }

    static inline bool bmp_test(uint64_t page) {
        return (pmm_bitmap[page / 8] & (1 << (page % 8))) != 0;
    }

    // Save/restore interrupt flag for thread safety
    static inline uint64_t irq_save() {
        uint64_t flags;
        asm volatile("pushfq; pop %0; cli" : "=r"(flags));
        return flags;
    }

    static inline void irq_restore(uint64_t flags) {
        asm volatile("push %0; popfq" : : "r"(flags) : "memory");
    }

    static bool pmm_init() {
        if (!memmap_req.response || !hhdm_req.response) return false;
        hhdm = hhdm_req.response->offset;

        struct limine_memmap_response* mm = memmap_req.response;

        // Pass 1: Find the highest physical address and total usable memory
        uint64_t highest_addr = 0;
        pmm_total = 0;

        for (uint64_t i = 0; i < mm->entry_count; i++) {
            struct limine_memmap_entry* e = mm->entries[i];
            uint64_t top = e->base + e->length;
            if (top > highest_addr) highest_addr = top;
            if (e->type == LIMINE_MEMMAP_USABLE) {
                pmm_total += e->length / PAGE_SIZE;
            }
        }

        pmm_highest_page = highest_addr / PAGE_SIZE;
        pmm_bitmap_bytes = (pmm_highest_page + 7) / 8;

        // Pass 2: Find a usable region big enough to hold the bitmap
        uint64_t bitmap_phys = 0;
        for (uint64_t i = 0; i < mm->entry_count; i++) {
            struct limine_memmap_entry* e = mm->entries[i];
            if (e->type == LIMINE_MEMMAP_USABLE && e->length >= pmm_bitmap_bytes) {
                bitmap_phys = e->base;
                break;
            }
        }
        if (bitmap_phys == 0) return false;

        pmm_bitmap = (uint8_t*)phys_to_virt(bitmap_phys);

        // Mark EVERYTHING as used (1). We'll free usable regions below.
        for (uint64_t i = 0; i < pmm_bitmap_bytes; i++) {
            pmm_bitmap[i] = 0xFF;
        }
        pmm_used = pmm_highest_page;

        // Pass 3: Mark usable regions as free (0)
        for (uint64_t i = 0; i < mm->entry_count; i++) {
            struct limine_memmap_entry* e = mm->entries[i];
            if (e->type != LIMINE_MEMMAP_USABLE) continue;

            uint64_t base_page = e->base / PAGE_SIZE;
            uint64_t page_count = e->length / PAGE_SIZE;
            for (uint64_t p = 0; p < page_count; p++) {
                bmp_clear(base_page + p);
                pmm_used--;
            }
        }

        // Pass 4: Re-mark the bitmap's own pages as used
        uint64_t bmp_pages = (pmm_bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t bmp_start = bitmap_phys / PAGE_SIZE;
        for (uint64_t p = 0; p < bmp_pages; p++) {
            if (!bmp_test(bmp_start + p)) {
                bmp_set(bmp_start + p);
                pmm_used++;
            }
        }

        pmm_search_hint = 0;
        return true;
    }

    uint64_t pmm_alloc_page() {
        uint64_t flags = irq_save();

        // Search from hint forward
        for (uint64_t i = pmm_search_hint; i < pmm_highest_page; i++) {
            if (!bmp_test(i)) {
                bmp_set(i);
                pmm_used++;
                pmm_search_hint = i + 1;
                irq_restore(flags);

                // Zero the page
                uint8_t* virt = (uint8_t*)phys_to_virt(i * PAGE_SIZE);
                for (uint64_t j = 0; j < PAGE_SIZE; j++) virt[j] = 0;

                return i * PAGE_SIZE;
            }
        }

        // Wrap around
        for (uint64_t i = 0; i < pmm_search_hint; i++) {
            if (!bmp_test(i)) {
                bmp_set(i);
                pmm_used++;
                pmm_search_hint = i + 1;
                irq_restore(flags);

                uint8_t* virt = (uint8_t*)phys_to_virt(i * PAGE_SIZE);
                for (uint64_t j = 0; j < PAGE_SIZE; j++) virt[j] = 0;

                return i * PAGE_SIZE;
            }
        }

        irq_restore(flags);
        return 0;  // Out of memory
    }

    void pmm_free_page(uint64_t phys) {
        uint64_t page = phys / PAGE_SIZE;
        if (page >= pmm_highest_page) return;

        uint64_t flags = irq_save();
        if (bmp_test(page)) {
            bmp_clear(page);
            pmm_used--;
            // Reset hint if this page is before it, for better reuse
            if (page < pmm_search_hint) pmm_search_hint = page;
        }
        irq_restore(flags);
    }

    uint64_t pmm_total_pages() { return pmm_total; }
    uint64_t pmm_used_pages()  { return pmm_used; }
    uint64_t pmm_free_pages()  { return pmm_total > pmm_used ? pmm_total - pmm_used : 0; }
    uint64_t pmm_total_bytes() { return pmm_total * PAGE_SIZE; }

    // ================================================================
    // Kernel Heap Allocator — Free-list with split & coalesce
    //
    // Layout:  [Block][user data][Block][user data]...
    //
    // The heap lives in a contiguous virtual region backed by
    // PMM pages. It starts at HEAP_INITIAL_PAGES and grows
    // on demand up to HEAP_MAX_PAGES.
    // ================================================================

    static constexpr uint64_t HEAP_INITIAL_PAGES = 16;   // 64KB
    static constexpr uint64_t HEAP_MAX_PAGES = 1024;     // 4MB max
    static constexpr uint64_t HEAP_GROW_PAGES = 16;      // Grow 64KB at a time
    static constexpr uint32_t BLOCK_MAGIC = 0xCAFEB00C;
    static constexpr uint64_t MIN_SPLIT = 32;            // Don't split if remainder < this

    struct Block {
        uint32_t magic;     // Corruption detector
        bool     free;
        uint64_t size;      // Size of user data (not including this header)
        Block*   next;      // Next block in linear order
    };

    static Block*   heap_head = nullptr;
    static uint8_t* heap_region_start = nullptr;  // Virtual start of heap region
    static uint64_t heap_committed = 0;           // Committed bytes
    static uint64_t heap_max_bytes = 0;           // Max bytes
    static uint64_t heap_phys_base = 0;           // Physical base of heap region

    static uint64_t stat_alloc_count = 0;
    static uint64_t stat_alloc_bytes = 0;

    static bool heap_init() {
        // Find the largest usable region for the heap
        struct limine_memmap_response* mm = memmap_req.response;
        uint64_t best_base = 0, best_length = 0;

        for (uint64_t i = 0; i < mm->entry_count; i++) {
            struct limine_memmap_entry* e = mm->entries[i];
            if (e->type == LIMINE_MEMMAP_USABLE && e->length > best_length) {
                best_base = e->base;
                best_length = e->length;
            }
        }

        if (best_length < HEAP_INITIAL_PAGES * PAGE_SIZE) return false;

        // Skip past the PMM bitmap if it's at the start of this region
        uint64_t bitmap_phys = virt_to_phys(pmm_bitmap);
        uint64_t bitmap_end_phys = bitmap_phys + pmm_bitmap_bytes;
        uint64_t heap_phys = best_base;

        if (heap_phys >= bitmap_phys && heap_phys < bitmap_end_phys) {
            heap_phys = (bitmap_end_phys + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        }

        // Calculate max heap size within this region
        uint64_t remaining = best_base + best_length - heap_phys;
        heap_max_bytes = remaining;
        if (heap_max_bytes > HEAP_MAX_PAGES * PAGE_SIZE) {
            heap_max_bytes = HEAP_MAX_PAGES * PAGE_SIZE;
        }

        heap_phys_base = heap_phys;
        heap_region_start = (uint8_t*)phys_to_virt(heap_phys);

        // Reserve initial pages in the PMM
        uint64_t start_page = heap_phys / PAGE_SIZE;
        for (uint64_t p = 0; p < HEAP_INITIAL_PAGES; p++) {
            if (!bmp_test(start_page + p)) {
                bmp_set(start_page + p);
                pmm_used++;
            }
        }

        heap_committed = HEAP_INITIAL_PAGES * PAGE_SIZE;

        // Create one big free block spanning the initial heap
        heap_head = (Block*)heap_region_start;
        heap_head->magic = BLOCK_MAGIC;
        heap_head->free = true;
        heap_head->size = heap_committed - sizeof(Block);
        heap_head->next = nullptr;

        stat_alloc_count = 0;
        stat_alloc_bytes = 0;

        return true;
    }

    // Grow the heap by allocating more pages from the reserved region
    static bool heap_grow(uint64_t min_bytes) {
        uint64_t grow = HEAP_GROW_PAGES * PAGE_SIZE;
        if (grow < min_bytes + sizeof(Block)) {
            grow = ((min_bytes + sizeof(Block)) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        }

        if (heap_committed + grow > heap_max_bytes) {
            grow = heap_max_bytes - heap_committed;
        }
        if (grow < sizeof(Block) + MIN_SPLIT) return false;

        // Reserve the new pages in PMM
        uint64_t new_phys = heap_phys_base + heap_committed;
        uint64_t start_page = new_phys / PAGE_SIZE;
        uint64_t page_count = grow / PAGE_SIZE;

        for (uint64_t p = 0; p < page_count; p++) {
            if (!bmp_test(start_page + p)) {
                bmp_set(start_page + p);
                pmm_used++;
            }
        }

        // Create a new free block at the growth point
        Block* new_block = (Block*)(heap_region_start + heap_committed);
        new_block->magic = BLOCK_MAGIC;
        new_block->free = true;
        new_block->size = grow - sizeof(Block);
        new_block->next = nullptr;

        // Link it to the end of the block list
        Block* last = heap_head;
        while (last->next) last = last->next;
        last->next = new_block;

        // Coalesce with the previous block if it's free and adjacent
        uint8_t* last_end = (uint8_t*)(last + 1) + last->size;
        if (last->free && last_end == (uint8_t*)new_block) {
            last->size += sizeof(Block) + new_block->size;
            last->next = new_block->next;
        }

        heap_committed += grow;
        return true;
    }

    void* kmalloc(uint64_t size) {
        if (size == 0) return nullptr;

        // Align to 16 bytes
        size = (size + 15) & ~15ULL;

        uint64_t flags = irq_save();

        // First-fit search
        Block* current = heap_head;
        while (current) {
            if (current->magic != BLOCK_MAGIC) {
                // Heap corruption detected
                irq_restore(flags);
                console_print("\n[HEAP] CORRUPTION: bad magic in kmalloc!");
                return nullptr;
            }

            if (current->free && current->size >= size) {
                // Can we split this block?
                if (current->size >= size + sizeof(Block) + MIN_SPLIT) {
                    // Split: create a new free block after the allocated portion
                    Block* remainder = (Block*)((uint8_t*)(current + 1) + size);
                    remainder->magic = BLOCK_MAGIC;
                    remainder->free = true;
                    remainder->size = current->size - size - sizeof(Block);
                    remainder->next = current->next;

                    current->size = size;
                    current->next = remainder;
                }

                current->free = false;
                stat_alloc_count++;
                stat_alloc_bytes += current->size;

                irq_restore(flags);
                return (void*)(current + 1);  // User data starts after header
            }

            current = current->next;
        }

        // No suitable block found — try to grow the heap
        irq_restore(flags);

        if (heap_grow(size)) {
            return kmalloc(size);  // Retry after growth
        }

        return nullptr;  // Out of memory
    }

    void kfree(void* ptr) {
        if (!ptr) return;

        Block* block = ((Block*)ptr) - 1;

        uint64_t flags = irq_save();

        if (block->magic != BLOCK_MAGIC) {
            irq_restore(flags);
            console_print("\n[HEAP] CORRUPTION: bad magic in kfree!");
            return;
        }

        if (block->free) {
            irq_restore(flags);
            console_print("\n[HEAP] Double free detected!");
            return;
        }

        block->free = true;
        stat_alloc_count--;
        stat_alloc_bytes -= block->size;

        // Coalesce with the NEXT block if it's free
        if (block->next && block->next->magic == BLOCK_MAGIC && block->next->free) {
            block->size += sizeof(Block) + block->next->size;
            block->next = block->next->next;
        }

        // Coalesce with the PREVIOUS block if it's free
        // (Requires walking from head — O(n) but simple)
        Block* prev = nullptr;
        Block* cur = heap_head;
        while (cur && cur != block) {
            prev = cur;
            cur = cur->next;
        }
        if (prev && prev->magic == BLOCK_MAGIC && prev->free) {
            uint8_t* prev_end = (uint8_t*)(prev + 1) + prev->size;
            if (prev_end == (uint8_t*)block) {
                prev->size += sizeof(Block) + block->size;
                prev->next = block->next;
            }
        }

        irq_restore(flags);
    }

    void* kcalloc(uint64_t count, uint64_t elem_size) {
        uint64_t total = count * elem_size;
        void* ptr = kmalloc(total);
        if (ptr) {
            uint8_t* p = (uint8_t*)ptr;
            for (uint64_t i = 0; i < total; i++) p[i] = 0;
        }
        return ptr;
    }

    // ================================================================
    // Heap diagnostics
    // ================================================================

    uint64_t heap_total_size()      { return heap_committed; }
    uint64_t heap_allocated_bytes() { return stat_alloc_bytes; }
    uint64_t heap_alloc_count()     { return stat_alloc_count; }

    uint64_t heap_free_bytes() {
        uint64_t free = 0;
        Block* b = heap_head;
        while (b) {
            if (b->free) free += b->size;
            b = b->next;
        }
        return free;
    }

    uint64_t heap_block_count() {
        uint64_t count = 0;
        Block* b = heap_head;
        while (b) { count++; b = b->next; }
        return count;
    }

    uint64_t heap_largest_free() {
        uint64_t largest = 0;
        Block* b = heap_head;
        while (b) {
            if (b->free && b->size > largest) largest = b->size;
            b = b->next;
        }
        return largest;
    }

    // ================================================================
    // Compatibility shims (old API used by shell meminfo)
    // ================================================================

    uint64_t get_total_pages() { return pmm_total; }
    uint64_t get_used_pages()  { return pmm_used; }
    uint64_t get_free_pages()  { return pmm_free_pages(); }
    uint64_t get_heap_used()   { return stat_alloc_bytes; }

    // ================================================================
    // Top-level init
    // ================================================================

    bool init() {
        if (!pmm_init()) {
            console_print("\n[MEM] PMM init failed!");
            return false;
        }

        char buf[32];
        console_print("\n[PMM] ");
        itoa(pmm_total, buf); console_print(buf);
        console_print(" pages (");
        itoa(pmm_total * PAGE_SIZE / 1024, buf); console_print(buf);
        console_print(" KB), ");
        itoa(pmm_free_pages(), buf); console_print(buf);
        console_print(" free, bitmap ");
        itoa(pmm_bitmap_bytes, buf); console_print(buf);
        console_print(" bytes");

        if (!heap_init()) {
            console_print("\n[HEAP] Heap init failed!");
            return false;
        }

        console_print("\n[HEAP] ");
        itoa(heap_committed / 1024, buf); console_print(buf);
        console_print(" KB initial, ");
        itoa(heap_max_bytes / 1024, buf); console_print(buf);
        console_print(" KB max");

        return true;
    }

}

#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Memory {
    bool init();
    void* alloc_page();
    void free_page(void* addr);
    void* kmalloc(uint64_t size);
    void kfree(void* ptr);
    uint64_t get_total_pages();
    uint64_t get_used_pages();
    uint64_t get_free_pages();
    uint64_t get_heap_used();
}

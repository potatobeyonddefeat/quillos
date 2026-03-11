#include "filesystem.h"
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

namespace QuillFS {
    volatile struct limine_memmap_request memmap_request = {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    volatile struct limine_module_request module_request = {
        .id = LIMINE_MODULE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    bool Filesystem::init() {
        // 1. Check if Limine loaded our RAM disk (module)
        if (!module_request.response || module_request.response->module_count == 0) {
            return false;
        }

        this->disk_base_address = reinterpret_cast<uintptr_t>(module_request.response->modules[0]->address);

        Superblock* sblck = reinterpret_cast<Superblock*>(this->disk_base_address);

        if (sblck->magic != QUILL_MAGIC) {
            return false; 
        }

        this->total_blocks = sblck->total_blocks;
        return true;
    };

    void* Filesystem::quick_allocate(size_t requested_size) {

        size_t blocks_needed = (requested_size + (BLOCK_SIZE - 1)) / BLOCK_SIZE;

        QuillFS::Superblock* disk_header = reinterpret_cast<QuillFS::Superblock*>(disk_base_address);

        this->total_blocks = disk_header->total_blocks;

        for(size_t i = 2; i < total_blocks; i++) {
            if (is_block_free(i)) {
                // For now, we assume it fits; mark it taken
                mark_used(i, blocks_needed);
                // Return the address: Start + (Index * 4096)
                return reinterpret_cast<void*>(this->disk_base_address + (i * BLOCK_SIZE));
            }
        }
        return nullptr; // No suitable block found
    };
    bool Filesystem::is_block_free(uint64_t block_index) { 
        return true; //placeholder implementation, should check the bitmap to see if the block is free
    };
    void Filesystem::mark_used(uint64_t block_index, size_t count) { 
        // Mark blocks as used in the bitmap
    };
    void Filesystem::optimize() { 
        // Optimization logic to reduce fragmentation would go here
    };
    
}
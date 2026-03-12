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

        this->block_bitmap.buffer = reinterpret_cast<uint64_t*>(disk_base_address + sizeof(Superblock));

        this->block_bitmap.size = (this->total_blocks + 63) / 64; 

        return true;
    };

    void* Filesystem::quick_allocate(size_t requested_size) {
        QuillFS::Superblock* disk_header = reinterpret_cast<QuillFS::Superblock*>(disk_base_address);
        size_t blocks_needed = (requested_size + (BLOCK_SIZE - 1)) / BLOCK_SIZE;

        // Calculate how many blocks the Superblock + Bitmap occupy
        size_t bitmap_bytes = (this->total_blocks + 7) / 8;
        size_t metadata_bytes = sizeof(Superblock) + bitmap_bytes;
        size_t first_usable_block = (metadata_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
        this->total_blocks = disk_header->total_blocks;

        for(size_t i = 0; i < block_bitmap.size; i++) {
            // If the 64-bit chunk is NOT all 1s, there is at least one free block here
            if (block_bitmap.buffer[i] != 0xFFFFFFFFFFFFFFFF) {
                
                // Use a built-in CPU instruction to find the first '0' bit
                // __builtin_ctzl finds "Count Trailing Zeros" (the first 0 from the right)
                // We invert the number (~) so the first 0 becomes the first 1
                uint64_t first_free_bit = __builtin_ctzl(~block_bitmap.buffer[i]);
        
                uint64_t absolute_index = (i * 64) + first_free_bit;
                
                if(absolute_index < first_usable_block) {
                    continue; // Skip blocks reserved for metadata
                }
                // Double check we haven't gone past the total_blocks limit
                if (absolute_index < total_blocks) {
                     mark_used(absolute_index, blocks_needed);
                     return reinterpret_cast<void*>(this->disk_base_address + (absolute_index * BLOCK_SIZE));
                }
            }
        }
        return nullptr; // No suitable block found
    };
    bool Filesystem::is_block_free(uint64_t block_index) { 
        // Using ! because [] returns true if bit is 1 (Used)
        // So if bit is 0 (False), the block is free.
        return !block_bitmap[block_index]; 
    }

    void Filesystem::mark_used(uint64_t block_index, size_t count) { 
        for(size_t i = 0; i < count; i++) {
            // Mark as true to indicate it is now occupied
            block_bitmap.set(block_index + i, true);
        }
    }
    void Filesystem::optimize() { 
        // Optimization logic to reduce fragmentation would go here
    };
    
}
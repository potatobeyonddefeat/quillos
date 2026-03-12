#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "Bitmap.h"
namespace QuillFS {
    extern volatile struct limine_memmap_request memmap_request;
    static constexpr uint64_t QUILL_MAGIC = 0x5175696C6C4F5300; //QuillOS in hex
    static constexpr uint32_t BLOCK_SIZE = 4096; // 4KB blocks
    struct [[gnu::packed]] Superblock {
        uint64_t magic;
        uint64_t total_blocks;
        uint64_t block_size;
        uint64_t first_data_block;
    };
    
    class Filesystem {
        public:
            Filesystem() = default;
            void* quick_allocate(size_t size); //first fit allocation strategy, returns a pointer to the allocated memory or nullptr if allocation fails
            void optimize(); //reorganize the filesystem to reduce fragmentation and improve allocation efficiency
            bool init(); //initialize the filesystem, read the superblock, and set up the bitmap
        private:
            uint64_t total_blocks;
            uint8_t* bitmap;
            uintptr_t disk_base_address = 0;
            bool is_block_free(uint64_t block_index);
            void mark_used(uint64_t block_index, size_t count);
            Bitmap block_bitmap;
    };
}
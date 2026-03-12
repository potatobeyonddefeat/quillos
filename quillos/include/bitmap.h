#include <stdint.h>
#include <stddef.h>
class Bitmap {
    public:
        uint64_t* buffer;
        size_t size; // in bits

        bool operator[](uint64_t index); // Check if bit at index is set

        bool set(uint64_t index, bool value);

    private:

};

class DiskBitmap : public Bitmap {
    public:
        uint64_t size_in_bits; // Total number of blocks (bits) on the disk

        uint64_t allocate_block(); // Find and allocate a free block, return its index

    private:
        void sync_to_disk(uint64_t block_index); // Write the updated bitmap to disk immediately
};
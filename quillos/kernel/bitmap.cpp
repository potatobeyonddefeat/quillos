#include "bitmap.h"
#include "filesystem.h"
#include <stdint.h>

bool Bitmap::operator[](uint64_t index) {
    if (index >= size * 64) return false; // Bounds check

    uint64_t elementIndex = index / 64; // Each uint64_t has 64 bits
    uint64_t bitIndex = index % 64;
    
    return (buffer[elementIndex] & (1ULL << bitIndex)) != 0;
}

bool Bitmap::set(uint64_t index, bool value) {
    if (index >= size * 64) return false;

    uint64_t elementIndex = index / 64;
    uint64_t bitIndex = index % 64;
    uint64_t bitMask = (1ULL << bitIndex);

    if (value) {
        // Mark as used (1)
        buffer[elementIndex] |= bitMask;
    } else {
        // Mark as free (0)
        buffer[elementIndex] &= ~bitMask;
    }

    return true;
}
uint64_t DiskBitmap::allocate_block() {
    for (uint64_t i = 0; i < size_in_bits; i++) {
        if (this->operator[](i) == false) { // Found a free block (0)
            this->set(i, true);           // Mark as used (1)
            sync_to_disk(i);               // CRITICAL: Update the disk immediately
            return i;                      // Return the block index
        }
    }
    return 0; // Disk Full
}
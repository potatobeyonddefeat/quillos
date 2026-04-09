#pragma once
#include <stdint.h>

namespace Disk {

    // ================================================================
    // Block device info (populated by IDENTIFY)
    // ================================================================
    struct BlockDevice {
        bool     present;
        char     model[41];     // Model string (null-terminated)
        char     serial[21];    // Serial number
        uint32_t total_sectors; // LBA28 addressable sectors
        uint32_t size_mb;       // Size in megabytes
        bool     lba_supported; // True if LBA mode available
    };

    // ================================================================
    // Initialization — detect ATA drive on primary bus
    // ================================================================
    bool init();

    // ================================================================
    // Block I/O — LBA28 PIO mode
    //
    // All buffers must be at least 512 bytes per sector.
    // LBA is a 28-bit logical block address (max ~128GB).
    // Returns true on success, false on error or no drive.
    // ================================================================
    bool read_sector(uint32_t lba, void* buffer);
    bool read_sectors(uint32_t lba, uint32_t count, void* buffer);
    bool write_sector(uint32_t lba, const void* buffer);

    // ================================================================
    // Queries
    // ================================================================
    bool is_present();
    const BlockDevice* get_info();
}

#pragma once
#include <stdint.h>

namespace Disk {
    bool init();
    bool read_sector(uint32_t lba, void* buffer);
    bool write_sector(uint32_t lba, const void* buffer);
    bool is_present();
}

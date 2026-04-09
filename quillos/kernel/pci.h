#pragma once
#include <stdint.h>

namespace PCI {
    struct Device {
        uint8_t bus, device, function;
        uint16_t vendor_id, device_id;
        uint8_t class_code, subclass;
        bool valid;
    };

    bool init();
    uint32_t get_count();
    const Device* get_device(uint32_t idx);
    const Device* find_device(uint8_t class_code, uint8_t subclass);
    uint32_t config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
}

#include "pci.h"
#include "io.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace PCI {

    static constexpr uint32_t MAX_DEVICES = 32;
    static Device devices[MAX_DEVICES];
    static uint32_t device_count = 0;

    uint32_t config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
        uint32_t addr = (1u << 31)
            | ((uint32_t)bus << 16)
            | ((uint32_t)dev << 11)
            | ((uint32_t)func << 8)
            | (offset & 0xFC);
        outl(0xCF8, addr);
        return inl(0xCFC);
    }

    bool init() {
        device_count = 0;

        for (uint16_t bus = 0; bus < 256; bus++) {
            for (uint8_t dev = 0; dev < 32; dev++) {
                uint32_t reg0 = config_read((uint8_t)bus, dev, 0, 0);
                uint16_t vendor = reg0 & 0xFFFF;
                if (vendor == 0xFFFF || vendor == 0x0000) continue;

                uint16_t devid = (reg0 >> 16) & 0xFFFF;
                uint32_t reg2 = config_read((uint8_t)bus, dev, 0, 8);
                uint8_t cls = (reg2 >> 24) & 0xFF;
                uint8_t sub = (reg2 >> 16) & 0xFF;

                if (device_count < MAX_DEVICES) {
                    devices[device_count].bus = (uint8_t)bus;
                    devices[device_count].device = dev;
                    devices[device_count].function = 0;
                    devices[device_count].vendor_id = vendor;
                    devices[device_count].device_id = devid;
                    devices[device_count].class_code = cls;
                    devices[device_count].subclass = sub;
                    devices[device_count].valid = true;
                    device_count++;
                }
            }
        }

        char buf[32];
        console_print("\n[PCI] Found ");
        itoa(device_count, buf);
        console_print(buf);
        console_print(" devices");

        return true;
    }

    uint32_t get_count() { return device_count; }

    const Device* get_device(uint32_t idx) {
        return idx < device_count ? &devices[idx] : nullptr;
    }

    const Device* find_device(uint8_t class_code, uint8_t subclass) {
        for (uint32_t i = 0; i < device_count; i++) {
            if (devices[i].class_code == class_code && devices[i].subclass == subclass)
                return &devices[i];
        }
        return nullptr;
    }
}

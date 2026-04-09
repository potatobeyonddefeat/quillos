#pragma once
#include <stdint.h>

namespace Network {
    struct MACAddress {
        uint8_t bytes[6];
    };

    bool init();
    void detect_nic(uint16_t vendor, uint16_t device);
    bool send_packet(const uint8_t* data, uint16_t length);
    bool receive_packet(uint8_t* data, uint16_t* length);
    bool is_present();
    uint32_t get_ip();
    void set_ip(uint32_t ip);
    const MACAddress* get_mac();
}

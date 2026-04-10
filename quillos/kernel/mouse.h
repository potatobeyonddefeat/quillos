#pragma once
#include <stdint.h>

namespace Mouse {
    static constexpr uint8_t BTN_LEFT   = 0x01;
    static constexpr uint8_t BTN_RIGHT  = 0x02;
    static constexpr uint8_t BTN_MIDDLE = 0x04;

    bool init();
    int32_t get_x();
    int32_t get_y();
    uint8_t get_buttons();
    uint64_t get_packet_count();
    bool is_present();
}

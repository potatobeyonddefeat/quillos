#pragma once
#include <stdint.h>

namespace E1000 {
    bool init();
    bool send(const uint8_t* data, uint16_t len);
    bool poll_receive(uint8_t* data, uint16_t* len);
    const uint8_t* get_mac();
    bool is_ready();
}

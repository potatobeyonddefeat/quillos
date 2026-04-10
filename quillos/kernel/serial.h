#pragma once
#include <stdint.h>

namespace Serial {
    bool init();
    bool data_available();
    char read_char();
    void write_char(char c);
    void write_str(const char* s);
    uint32_t read_available(char* buf, uint32_t buf_size);
    uint32_t read_line(char* buf, uint32_t buf_size, uint32_t timeout_ms);
}

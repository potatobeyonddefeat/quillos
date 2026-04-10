#pragma once
#include <stdint.h>

namespace Clipboard {
    static constexpr uint32_t MAX_SIZE = 4096;

    void init();
    void set(const char* data, uint32_t len);
    void set_str(const char* str);
    const char* get();
    uint32_t size();
    bool empty();
    void clear();
}

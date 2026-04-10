#include "clipboard.h"

extern void console_print(const char* str);

namespace Clipboard {

    static char buffer[MAX_SIZE];
    static uint32_t length = 0;

    void init() {
        for (uint32_t i = 0; i < MAX_SIZE; i++) buffer[i] = 0;
        length = 0;
        console_print("\n[CLIP] Clipboard ready");
    }

    void set(const char* data, uint32_t len) {
        if (!data) { clear(); return; }
        if (len >= MAX_SIZE) len = MAX_SIZE - 1;
        for (uint32_t i = 0; i < len; i++) buffer[i] = data[i];
        buffer[len] = '\0';
        length = len;
    }

    void set_str(const char* str) {
        if (!str) { clear(); return; }
        uint32_t len = 0;
        while (str[len]) len++;
        set(str, len);
    }

    const char* get() { return buffer; }
    uint32_t size() { return length; }
    bool empty() { return length == 0; }
    void clear() { buffer[0] = '\0'; length = 0; }
}

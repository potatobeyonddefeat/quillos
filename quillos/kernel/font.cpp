#include <stdint.h>
#include "font.h"

/* Standard 8x8 ASCII font */

const uint8_t font[128][8] = {
#include "font8x8_basic.inl"
};

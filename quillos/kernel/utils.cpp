#include <stdint.h>

void itoa(uint64_t n, char* str) {
    char tmp[21];
    int i = 0;
    if (n == 0) tmp[i++] = '0';
    while (n > 0) {
        tmp[i++] = (n % 10) + '0';
        n /= 10;
    }
    int j = 0;
    while (i > 0) str[j++] = tmp[--i];
    str[j] = '\0';
}

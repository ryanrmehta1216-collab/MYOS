#include <stdint.h>
#include <stddef.h>

void* memset(void* dest, int val, size_t count) {
    uint8_t* temp = (uint8_t*)dest;
    for (; count != 0; count--) {
        *temp++ = (uint8_t)val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t count) {
    const uint8_t* sp = (const uint8_t*)src;
    uint8_t* dp = (uint8_t*)dest;
    for (; count != 0; count--) {
        *dp++ = *sp++;
    }
    return dest;
}
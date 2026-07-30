#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

/* Standard memory operations for freestanding kernel */
void* memset(void* dest, int val, size_t count);
void* memcpy(void* dest, const void* src, size_t count);

#endif /* MEMORY_H */

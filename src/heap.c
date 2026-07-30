#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------
 * Simple Kernel Heap Allocator (kmalloc / kfree)
 * 
 * Uses a linked list of free/allocated blocks within
 * a fixed 4MB region at 0x01000000 (16 MB).
 * Blocks are coalesced on free() to prevent fragmentation.
 * ----------------------------------------------- */

#define HEAP_START  0x01000000  /* 16 MB */
#define HEAP_SIZE   0x00400000  /* 4 MB */

typedef struct heap_block {
    size_t  size;       /* Usable size (excluding header) */
    uint8_t is_free;    /* 1 = free, 0 = allocated */
    struct heap_block* next;
} __attribute__((packed)) heap_block_t;

static heap_block_t* heap_head = NULL;

void init_heap(void) {
    /* Initially, the entire heap is one giant free block */
    heap_head = (heap_block_t*)HEAP_START;
    heap_head->size    = HEAP_SIZE - sizeof(heap_block_t);
    heap_head->is_free = 1;
    heap_head->next    = NULL;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align to 4 bytes */
    size = (size + 3) & ~3;

    heap_block_t* curr = heap_head;
    while (curr != NULL) {
        if (curr->is_free && curr->size >= size) {
            /* Split if there's room for another header */
            if (curr->size >= size + sizeof(heap_block_t) + 16) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)curr + sizeof(heap_block_t) + size);
                new_block->size   = curr->size - size - sizeof(heap_block_t);
                new_block->is_free = 1;
                new_block->next   = curr->next;

                curr->size = size;
                curr->next = new_block;
            }

            curr->is_free = 0;
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }

    return NULL; /* Out of memory */
}

void kfree(void* ptr) {
    if (ptr == NULL) return;

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->is_free = 1;

    /* Coalesce adjacent free blocks */
    heap_block_t* curr = heap_head;
    while (curr != NULL && curr->next != NULL) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(heap_block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

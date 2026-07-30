#include "pmm.h"
#include "serial.h"
#include "aegis.h"

/* -----------------------------------------------
 * Physical Memory Manager (PMM) — Buddy Allocator
 *
 * Manages physical memory as a binary buddy system.
 *
 * Free lists: one per order (0 to MAX_BUDDY_ORDER).
 * Each entry in free_list[order] is the physical
 * address of a free block of size 2^order pages.
 *
 * Allocation: round up to next power of 2, then
 * split recursively from the smallest available block.
 *
 * Deallocation: merge buddies recursively if both
 * are free (double-free prevention via bitmap).
 *
 * The bitmap is preserved for the PMM Heap Visualizer,
 * which queries pmm_get_frame_state() for each frame.
 * ----------------------------------------------- */

/* A buddy allocator free-block node stored within the free page itself */
typedef struct buddy_node {
    struct buddy_node* next;
} buddy_node_t;

/* Free lists: one per order */
static buddy_node_t* free_lists[MAX_BUDDY_ORDER + 1];

/* Bitmap for visualizer queries (1 bit per page) */
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

/* Bounds of physical memory we manage */
static uint32_t pmm_end_addr   = 0;
static uint32_t pmm_total_pages = 0;
static size_t   pmm_free_pages  = 0;

/* ---- Buddy helper functions ---- */

/* Get the buddy address for a block at a given order */
static inline uint32_t buddy_of(uint32_t addr, int order) {
    uint32_t block_size = PAGE_SIZE << order;          /* bytes in this block */
    return addr ^ block_size;                          /* flip the order-th bit */
}

/* Mark a single page in the bitmap */
static inline void bitmap_set(uint32_t page, int used) {
    uint32_t byte_idx = page / 8;
    uint32_t bit_idx  = page % 8;
    if (byte_idx >= PMM_BITMAP_SIZE) return;
    if (used)
        pmm_bitmap[byte_idx] |=  (1 << bit_idx);
    else
        pmm_bitmap[byte_idx] &= ~(1 << bit_idx);
}

/* Mark a range of pages in the bitmap */
static void bitmap_set_range(uint32_t start_page, uint32_t count, int used) {
    for (uint32_t i = 0; i < count; i++) {
        bitmap_set(start_page + i, used);
    }
}

/* ---- Buddy Allocator Core ---- */

/* Initialize the PMM */
void pmm_init(uint32_t mem_lower, uint32_t mem_upper) {
    (void)mem_lower;
    write_serial("[PMM] Initializing Buddy Allocator...\r\n");

    /* Determine total memory */
    uint32_t top_mem_kb = mem_upper ? mem_upper : (32 * 1024);
    pmm_end_addr = top_mem_kb * 1024;
    pmm_total_pages = pmm_end_addr / PAGE_SIZE;
    if (pmm_total_pages > PMM_MAX_PAGES) {
        pmm_total_pages = PMM_MAX_PAGES;
        pmm_end_addr = pmm_total_pages * PAGE_SIZE;
    }

    /* Zero out free lists */
    for (int i = 0; i <= MAX_BUDDY_ORDER; i++) {
        free_lists[i] = NULL;
    }

    /* Zero bitmap (all free) */
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0;
    }

    pmm_free_pages = pmm_total_pages;

    /* ---- Mark reserved regions as used ---- */
    bitmap_set_range(0, 256, PMM_FRAME_USED);                       /* 0-1 MB */
    bitmap_set_range(0x80000 / PAGE_SIZE, 0x20000 / PAGE_SIZE, PMM_FRAME_USED); /* EBDA */
    bitmap_set_range(0xA0000 / PAGE_SIZE, 0x20000 / PAGE_SIZE, PMM_FRAME_USED); /* VGA */

    /* Mark kernel image as used */
    extern uint32_t end;
    uint32_t kernel_end_addr = (uint32_t)&end;
    uint32_t kernel_pages = (kernel_end_addr + PAGE_SIZE - 1) / PAGE_SIZE;
    bitmap_set_range(0, kernel_pages, PMM_FRAME_USED);

    /* Mark bitmap itself as used */
    uint32_t bitmap_start = (uint32_t)pmm_bitmap;
    uint32_t bitmap_end   = bitmap_start + PMM_BITMAP_SIZE;
    uint32_t bitmap_start_page = bitmap_start / PAGE_SIZE;
    uint32_t bitmap_page_count = (bitmap_end - bitmap_start + PAGE_SIZE - 1) / PAGE_SIZE;
    bitmap_set_range(bitmap_start_page, bitmap_page_count, PMM_FRAME_USED);

    /* ---- Add all free pages to the buddy free lists ---- */
    pmm_free_pages = 0;
    uint32_t i = 0;
    while (i < pmm_total_pages) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx  = i % 8;
        if (pmm_bitmap[byte_idx] & (1 << bit_idx)) {
            i++;
            continue;
        }
        /* Find largest power-of-two free block starting at i */
        int max_order = 0;
        uint32_t remaining = pmm_total_pages - i;
        while ((1U << (max_order + 1)) <= remaining &&
               max_order < MAX_BUDDY_ORDER &&
               (i & ((1U << (max_order + 1)) - 1)) == 0) {
            /* Check if all pages in 2^(max_order+1) are free */
            uint32_t block = 1U << (max_order + 1);
            int all_free = 1;
            for (uint32_t j = 0; j < block; j++) {
                uint32_t b = (i + j) / 8;
                uint32_t bi = (i + j) % 8;
                if (pmm_bitmap[b] & (1 << bi)) { all_free = 0; break; }
            }
            if (!all_free) break;
            max_order++;
        }

        uint32_t block_size = 1U << max_order;
        uint32_t addr = i * PAGE_SIZE;

        /* Add to free list */
        buddy_node_t* node = (buddy_node_t*)(uintptr_t)addr;
        node->next = free_lists[max_order];
        free_lists[max_order] = node;

        pmm_free_pages += block_size;
        i += block_size;
    }

    write_serial("[PMM] Total: ");
    write_serial_hex(pmm_total_pages);
    write_serial(" frames, Free: ");
    write_serial_hex(pmm_free_pages);
    write_serial(" frames (Buddy Allocator)\r\n");
}

/* Allocate a block of 2^order pages */
void* pmm_alloc_blocks(int order) {
    if (order < 0) order = 0;
    if (order > MAX_BUDDY_ORDER) return (void*)-1;

    /* Find a free block at 'order' or higher */
    int current_order = order;
    while (current_order <= MAX_BUDDY_ORDER && free_lists[current_order] == NULL) {
        current_order++;
    }

    if (current_order > MAX_BUDDY_ORDER) {
        write_serial("[PMM] OOM! No free blocks of order ");
        write_serial_hex(order);
        write_serial("\r\n");
        return (void*)-1;
    }

    /* Pop the free block */
    buddy_node_t* block = free_lists[current_order];
    free_lists[current_order] = block->next;

    /* Split the block down to the desired order */
    while (current_order > order) {
        current_order--;
        uint32_t block_addr = (uint32_t)block;
        uint32_t buddy_addr = block_addr + (PAGE_SIZE << current_order);

        /* Add the buddy (upper half) to the lower-order free list */
        buddy_node_t* buddy = (buddy_node_t*)(uintptr_t)buddy_addr;
        buddy->next = free_lists[current_order];
        free_lists[current_order] = buddy;
    }

    uint32_t page_start = (uint32_t)block / PAGE_SIZE;
    uint32_t num_pages = 1U << order;
    bitmap_set_range(page_start, num_pages, PMM_FRAME_USED);
    pmm_free_pages -= num_pages;

    return block;
}

/* Free a block of 2^order pages and merge with buddy if possible */
void pmm_free_blocks(void* phys_addr, int order) {
    if (order < 0) order = 0;
    if (order > MAX_BUDDY_ORDER) return;

    uint32_t addr = (uint32_t)phys_addr;
    if (addr % PAGE_SIZE != 0) {
        addr &= ~(PAGE_SIZE - 1);
    }

    uint32_t page = addr / PAGE_SIZE;
    uint32_t num_pages = 1U << order;

    /* Check if the bitmap already says free — double-free guard */
    uint32_t byte_idx = page / 8;
    uint32_t bit_idx  = page % 8;
    if (byte_idx < PMM_BITMAP_SIZE && !(pmm_bitmap[byte_idx] & (1 << bit_idx))) {
        write_serial("[PMM] Double-free detected at 0x");
        write_serial_hex(addr);
        write_serial("\r\n");
        return;
    }

    bitmap_set_range(page, num_pages, PMM_FRAME_FREE);
    pmm_free_pages += num_pages;

    /* Try to merge with buddy */
    while (order < MAX_BUDDY_ORDER) {
        uint32_t buddy_addr = buddy_of(addr, order);

        /* Check if buddy is free by looking at the bitmap */
        uint32_t buddy_page = buddy_addr / PAGE_SIZE;
        int buddy_free = 1;
        for (uint32_t i = 0; i < num_pages; i++) {
            uint32_t bp = buddy_page + i;
            uint32_t by = bp / 8;
            uint32_t bi = bp % 8;
            if (by >= PMM_BITMAP_SIZE || (pmm_bitmap[by] & (1 << bi))) {
                buddy_free = 0;
                break;
            }
        }

        if (!buddy_free) break;  /* Can't merge */

        /* Remove buddy from its free list */
        buddy_node_t** prev = &free_lists[order];
        while (*prev != NULL) {
            if ((uint32_t)*prev == buddy_addr) {
                *prev = (*prev)->next;
                break;
            }
            prev = &(*prev)->next;
        }

        /* Merge: use the lower address */
        if (buddy_addr < addr) addr = buddy_addr;
        order++;
        num_pages <<= 1;
    }

    /* Insert merged block into free list */
    buddy_node_t* node = (buddy_node_t*)(uintptr_t)addr;
    node->next = free_lists[order];
    free_lists[order] = node;
}

/* ---- Compatibility wrappers ---- */

void* pmm_alloc_frame(void) {
    return pmm_alloc_blocks(0);  /* order 0 = 1 page */
}

void pmm_free_frame(void* phys_addr) {
    pmm_free_blocks(phys_addr, 0);
}

size_t pmm_get_free_count(void) {
    return pmm_free_pages;
}

size_t pmm_get_total_count(void) {
    return pmm_total_pages;
}

void pmm_mark_used(void* phys_addr, size_t count) {
    uint32_t addr = (uint32_t)phys_addr;
    uint32_t page = addr / PAGE_SIZE;
    if (count == 0) return;
    bitmap_set_range(page, count, PMM_FRAME_USED);

    /* Recalculate free count */
    pmm_free_pages = 0;
    for (uint32_t k = 0; k < pmm_total_pages; k++) {
        uint32_t by = k / 8, bi = k % 8;
        if (!(pmm_bitmap[by] & (1 << bi))) pmm_free_pages++;
    }
}

void pmm_mark_free(void* phys_addr, size_t count) {
    uint32_t addr = (uint32_t)phys_addr;
    uint32_t page = addr / PAGE_SIZE;
    if (count == 0) return;
    bitmap_set_range(page, count, PMM_FRAME_FREE);
    pmm_free_pages += count;
}

int pmm_is_used(void* phys_addr) {
    uint32_t addr = (uint32_t)phys_addr;
    uint32_t page = addr / PAGE_SIZE;
    if (page >= pmm_total_pages) return 1;
    uint32_t byte_idx = page / 8;
    uint32_t bit_idx  = page % 8;
    if (byte_idx >= PMM_BITMAP_SIZE) return 1;
    return (pmm_bitmap[byte_idx] & (1 << bit_idx)) ? 1 : 0;
}

/* Visualizer interface — returns 0=free, 1=used, 2=quarantined */
int pmm_get_frame_state(uint32_t frame_index) {
    if (frame_index >= pmm_total_pages) return 1;

    uint32_t phys_addr = frame_index * PAGE_SIZE;

    /* Check Aegis quarantine */
    if (aegis_is_quarantined((void*)(uintptr_t)phys_addr)) {
        return 2;  /* Red: quarantined */
    }

    /* Check bitmap */
    uint32_t byte_idx = frame_index / 8;
    uint32_t bit_idx  = frame_index % 8;
    if (byte_idx >= PMM_BITMAP_SIZE) return 1;
    if (pmm_bitmap[byte_idx] & (1 << bit_idx)) {
        return 1;  /* Green: allocated */
    }

    return 0;  /* Grey: free */
}

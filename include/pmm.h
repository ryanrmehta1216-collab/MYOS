#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------
 * Physical Memory Manager (PMM) — Buddy Allocator
 *
 * Replaced the bitmap-based allocator with a
 * buddy allocation algorithm to prevent external
 * fragmentation. Allocates power-of-two page blocks.
 *
 * Each order manages a free list of 2^order pages.
 * Orders range from 0 (1 page = 4 KB) up to
 * MAX_BUDDY_ORDER (2^MAX_BUDDY_ORDER pages).
 * ----------------------------------------------- */

/* Page size: 4 KB (standard for x86 paging) */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* Assume up to 128 MB of physical RAM for now */
#define PMM_MAX_PAGES   (128 * 1024 * 1024 / PAGE_SIZE)  /* 32768 pages */

/* Total bitmap size in bytes (1 bit per page) — kept for visualizer compatibility */
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)

/* Maximum buddy order: 2^15 = 32768 pages = 128 MB */
#define MAX_BUDDY_ORDER 15

/* Mark a page as used/allocated */
#define PMM_FRAME_USED     1
#define PMM_FRAME_FREE     0

/* Initialize the PMM with the memory map from GRUB. */
void pmm_init(uint32_t mem_lower, uint32_t mem_upper);

/* Allocate a single physical page frame. Returns physical address (4K-aligned). */
void* pmm_alloc_frame(void);

/* Allocate 2^order contiguous page frames. Returns base physical address. */
void* pmm_alloc_blocks(int order);

/* Free a previously allocated physical page frame. */
void pmm_free_frame(void* phys_addr);

/* Free 2^order contiguous page frames starting at phys_addr. */
void pmm_free_blocks(void* phys_addr, int order);

/* Return the number of free page frames currently available. */
size_t pmm_get_free_count(void);

/* Return the total number of page frames managed. */
size_t pmm_get_total_count(void);

/* Mark a region of physical memory as used (reserved by hardware, kernel, etc.) */
void pmm_mark_used(void* phys_addr, size_t count);

/* Mark a region of physical memory as free */
void pmm_mark_free(void* phys_addr, size_t count);

/* Test if a specific frame is in use (returns 1 if used, 0 if free) */
int pmm_is_used(void* phys_addr);

/* Get the state of a frame by index for the visualizer.
 * Returns: 0 = free (grey), 1 = used/allocated (green), 2 = quarantined/bad (red) */
int pmm_get_frame_state(uint32_t frame_index);

#endif /* PMM_H */

#include <stdint.h>
#include "pmm.h"
#include "serial.h"

/* -----------------------------------------------
 * Paging subsystem for 32-bit x86.
 * 
 * Page Directory and Page Tables are 4-byte entries.
 * Each entry maps 4 KB (PAGE_SIZE).
 * A page directory has 1024 PDEs; each page table has 1024 PTEs.
 *
 * All page tables for the first 16 MB are statically allocated.
 * Page tables for memory beyond 16 MB are dynamically allocated
 * via pmm_alloc_frame() during init_paging() — this is safe
 * because PMM is already initialized before paging is enabled.
 *
 * After paging is enabled, the full physical memory range is
 * identity-mapped, so any PMM-allocated page remains accessible.
 * ----------------------------------------------- */

/* Page table entry flags */
#define PAGE_PRESENT    0x01
#define PAGE_WRITABLE   0x02
#define PAGE_USER       0x04
#define PAGE_WRITE_THRU 0x08
#define PAGE_CACHE_DIS  0x10
#define PAGE_ACCESSED   0x20
#define PAGE_DIRTY      0x40

/* Number of PDE entries in the page directory */
#define PDE_COUNT       1024

/* Kernel page directory (aligned to 4096) */
static uint32_t page_directory[PDE_COUNT] __attribute__((aligned(4096)));

/* Identity-mapped page tables for the first 16 MB (4 page tables, statically allocated) */
static uint32_t id_pt_0[1024] __attribute__((aligned(4096))); /* 0x00000000 - 0x003FFFFF */
static uint32_t id_pt_1[1024] __attribute__((aligned(4096))); /* 0x00400000 - 0x007FFFFF */
static uint32_t id_pt_2[1024] __attribute__((aligned(4096))); /* 0x00800000 - 0x00BFFFFF */
static uint32_t id_pt_3[1024] __attribute__((aligned(4096))); /* 0x00C00000 - 0x00FFFFFF */

/* Heap page table: maps virtual 0x01000000 - 0x013FFFFF (PDE index 4) */
static uint32_t heap_page_table[1024] __attribute__((aligned(4096)));

/* VBE framebuffer page table: maps virtual 0xFD000000 - 0xFD3FFFFF (PDE index 1012) */
static uint32_t fb_page_table[1024] __attribute__((aligned(4096)));

/* Kernel heap region (virtual) */
#define HEAP_VIRT_START  0x01000000  /* 16 MB */
#define HEAP_PTE_COUNT   1024        /* Full page table = 4 MB */

/* VBE framebuffer virtual address and size */
#define FB_VIRT_START    0xFD000000
#define FB_SIZE_BYTES    (1024 * 768 * 4)  /* 3,145,728 = ~3 MB */
#define FB_PTE_COUNT     ((FB_SIZE_BYTES + PAGE_SIZE - 1) / PAGE_SIZE)

/* Initialize a single page table entry with identity mapping */
static void map_page(uint32_t* pt, uint32_t idx, uint32_t phys_addr) {
    pt[idx] = (phys_addr & 0xFFFFF000) | PAGE_PRESENT | PAGE_WRITABLE;
}

/* Initialize paging subsystem — called BEFORE interrupts are enabled.
 *
 * NOW IDENTITY-MAPS THE FULL PHYSICAL MEMORY RANGE dynamically.
 * In addition to the static first-16-MB page tables, page tables for
 * higher physical addresses are allocated from the PMM and populated.
 * This ensures every page the buddy allocator can return is accessible.
 */
void init_paging(void) {
    write_serial("[PAGING] Initializing paging...\r\n");

    size_t total_pages = pmm_get_total_count();
    uint32_t total_mem_bytes = (uint32_t)(total_pages * PAGE_SIZE);  /* e.g. 128 MB */

    write_serial("[PAGING] Total physical memory: ");
    write_serial_hex(total_mem_bytes / (1024 * 1024));
    write_serial(" MB\r\n");

    /* ---- 1. Zero out the page directory ---- */
    for (int i = 0; i < PDE_COUNT; i++) {
        page_directory[i] = 0;
    }

    /* ---- 2. Identity map the first 16 MB using static page tables ---- */
    for (uint32_t addr = 0; addr < 0x400000; addr += PAGE_SIZE) {
        map_page(id_pt_0, addr / PAGE_SIZE, addr);
    }
    page_directory[0] = ((uint32_t)id_pt_0) | PAGE_PRESENT | PAGE_WRITABLE;

    for (uint32_t addr = 0x400000; addr < 0x800000; addr += PAGE_SIZE) {
        map_page(id_pt_1, (addr - 0x400000) / PAGE_SIZE, addr);
    }
    page_directory[1] = ((uint32_t)id_pt_1) | PAGE_PRESENT | PAGE_WRITABLE;

    for (uint32_t addr = 0x800000; addr < 0xC00000; addr += PAGE_SIZE) {
        map_page(id_pt_2, (addr - 0x800000) / PAGE_SIZE, addr);
    }
    page_directory[2] = ((uint32_t)id_pt_2) | PAGE_PRESENT | PAGE_WRITABLE;

    for (uint32_t addr = 0xC00000; addr < 0x1000000; addr += PAGE_SIZE) {
        map_page(id_pt_3, (addr - 0xC00000) / PAGE_SIZE, addr);
    }
    page_directory[3] = ((uint32_t)id_pt_3) | PAGE_PRESENT | PAGE_WRITABLE;

    /* ---- 3. Identity map memory from 16 MB up to the full PMM range ---- */
    /* Dynamically allocate page tables from the PMM for memory beyond 16 MB.
     * This is safe because pmm_init() has already run, and paging is not
     * yet enabled — physical addresses are directly accessible. */

    write_serial("[PAGING] Identity-mapping full memory range up to ");
    write_serial_hex(total_mem_bytes / (1024 * 1024));
    write_serial(" MB...\r\n");

    uint32_t pde_start = 4;                        /* First PDE beyond the static 4 (16 MB) */
    uint32_t pde_end   = (total_mem_bytes + 0x3FFFFF) >> 22;  /* Last PDE, rounded UP */
    if (pde_end > PDE_COUNT) pde_end = PDE_COUNT;
    if (pde_start > pde_end) pde_start = pde_end; /* Guard: <16 MB systems */

    for (uint32_t pde_idx = pde_start; pde_idx < pde_end; pde_idx++) {
        if (pde_idx == 4) continue;  /* Skip PDE 4 — heap page table overwrites it */

        /* Allocate a page table page from the PMM */
        uint32_t* pt = (uint32_t*)pmm_alloc_frame();
        if ((uint32_t)pt == (uint32_t)-1) {
            write_serial("[PAGING] WARNING: OOM allocating page table for PDE ");
            write_serial_hex(pde_idx);
            write_serial("\r\n");
            break;
        }

        /* Zero out the new page table */
        for (int j = 0; j < 1024; j++) {
            pt[j] = 0;
        }

        /* Identity-map all pages in this 4 MB block */
        uint32_t base = pde_idx << 22;
        uint32_t end  = base + 0x400000;
        if (end > total_mem_bytes) end = total_mem_bytes;

        for (uint32_t addr = base; addr < end; addr += PAGE_SIZE) {
            uint32_t pt_idx = (addr >> 12) & 0x3FF;
            map_page(pt, pt_idx, addr);
        }

        /* Install in page directory */
        page_directory[pde_idx] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    /* ---- 4. Pre-allocate and map the kernel heap (4 MB at 0x01000000) ---- */
    write_serial("[PAGING] Pre-allocating heap physical pages...\r\n");

    for (uint32_t i = 0; i < HEAP_PTE_COUNT; i++) {
        void* phys = pmm_alloc_frame();
        if ((uint32_t)phys == (uint32_t)-1) {
            write_serial("[PAGING] ERROR: Out of memory pre-allocating heap!\r\n");
            break;
        }
        heap_page_table[i] = ((uint32_t)phys) | PAGE_PRESENT | PAGE_WRITABLE;
    }
    page_directory[4] = ((uint32_t)heap_page_table) | PAGE_PRESENT | PAGE_WRITABLE;

    /* ---- 5. Map the VBE framebuffer at 0xFD000000 ---- */
    write_serial("[PAGING] Mapping VBE framebuffer...\r\n");

    for (uint32_t i = 0; i < FB_PTE_COUNT; i++) {
        uint32_t phys = FB_VIRT_START + i * PAGE_SIZE;
        fb_page_table[i] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DIS;
    }
    page_directory[FB_VIRT_START >> 22] = ((uint32_t)fb_page_table) | PAGE_PRESENT | PAGE_WRITABLE;

    /* ---- 6. Load the page directory and enable paging ---- */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_directory));

    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    write_serial("[PAGING] Paging enabled. ");
    write_serial_hex(total_mem_bytes / (1024 * 1024));
    write_serial(" MB identity-mapped, heap at 0x01000000.\r\n");
}

/* Map a contiguous region of physical memory to virtual memory.
 * After paging is enabled, this function can only create new mappings
 * for regions whose page tables are within the identity-mapped range. */
void paging_map_region(uint32_t phys_addr, uint32_t virt_addr, size_t size, uint32_t flags) {
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = 0; i < pages; i++) {
        uint32_t virt = virt_addr + i * PAGE_SIZE;
        uint32_t pd_idx = virt >> 22;
        uint32_t pt_idx = (virt >> 12) & 0x3FF;

        uint32_t pde = page_directory[pd_idx];
        if (!(pde & PAGE_PRESENT)) {
            write_serial("[PAGING] WARNING: Page table not present for paging_map_region.\r\n");
            return;
        }

        uint32_t table_phys = pde & 0xFFFFF000;
        uint32_t* pt = (uint32_t*)table_phys;

        pt[pt_idx] = ((phys_addr + i * PAGE_SIZE) & 0xFFFFF000) | (flags & 0xFFF) | PAGE_PRESENT;
        __asm__ volatile ("invlpg (%0)" : : "r"(virt));
    }
}

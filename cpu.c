#include <stdint.h>
#include <stddef.h>
#include "serial.h"

/* -----------------------------------------------
 * Global Descriptor Table (GDT)
 * 32-bit protected mode segments
 * ----------------------------------------------- */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

/* GDT indices */
#define GDT_NULL        0
#define GDT_KERNEL_CODE 1   /* 0x08 */
#define GDT_KERNEL_DATA 2   /* 0x10 */
#define GDT_USER_CODE   3   /* 0x1B */
#define GDT_USER_DATA   4   /* 0x23 */
#define GDT_TSS         5   /* 0x28 */

static gdt_entry_t gdt[6];
gdt_ptr_t          gdt_p;

/* TSS structure for ring 0 stack on interrupt */
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static tss_entry_t tss_entry;

static void set_gdt_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

void init_gdt(void) {
    write_serial("[GDT] Initializing GDT...\r\n");

    gdt_p.limit = sizeof(gdt_entry_t) * 6 - 1;
    gdt_p.base  = (uint32_t)&gdt;

    /* Null segment (required) */
    set_gdt_gate(GDT_NULL, 0, 0, 0, 0);

    /* Kernel code segment: base=0, limit=4GB, ring 0, code R/X */
    set_gdt_gate(GDT_KERNEL_CODE, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Kernel data segment: base=0, limit=4GB, ring 0, data R/W */
    set_gdt_gate(GDT_KERNEL_DATA, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* User code segment: base=0, limit=4GB, ring 3, code R/X */
    set_gdt_gate(GDT_USER_CODE, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* User data segment: base=0, limit=4GB, ring 3, data R/W */
    set_gdt_gate(GDT_USER_DATA, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* TSS segment (ring 0 stack pointer) */
    uint32_t tss_base  = (uint32_t)&tss_entry;
    uint32_t tss_limit = sizeof(tss_entry_t) - 1;

    /* 0x89 = Present, Ring 0, 32-bit TSS (available) */
    set_gdt_gate(GDT_TSS, tss_base, tss_limit, 0x89, 0x00);

    /* Flush the new GDT and reload segment registers */
    __asm__ volatile (
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        :
        : "m"(gdt_p)
        : "eax"
    );

    write_serial("[GDT] GDT loaded with 6 entries.\r\n");
}

void init_tss(uint32_t kernel_esp) {
    write_serial("[TSS] Initializing TSS...\r\n");

    /* Zero out the TSS */
    for (size_t i = 0; i < sizeof(tss_entry_t); i++) {
        ((uint8_t*)&tss_entry)[i] = 0;
    }

    /* Set kernel stack for ring 0 entry */
    tss_entry.ss0  = 0x10;  /* Kernel data segment */
    tss_entry.esp0 = kernel_esp;

    /* I/O Map Base: set to TSS limit to indicate no I/O permission bitmap */
    tss_entry.iomap_base = sizeof(tss_entry_t);

    /* Load Task Register (GDT index 5, so selector = 0x28) */
    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)(GDT_TSS * 8)));

    write_serial("[TSS] TSS loaded at selector 0x28.\r\n");
}

void set_kernel_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}

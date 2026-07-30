#include <stdint.h>
#include <stddef.h>
#include "interrupts.h"
#include "serial.h"
#include "panic.h"

/* -----------------------------------------------
 * IDT Entry & Pointer structures
 * ----------------------------------------------- */
typedef struct {
    uint16_t base_low;      /* Low 16 bits of handler address */
    uint16_t sel;           /* Kernel segment selector (0x08) */
    uint8_t  always0;       /* Reserved, must be 0 */
    uint8_t  flags;         /* Type, DPL, Present */
    uint16_t base_high;     /* High 16 bits of handler address */
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;         /* Size of IDT - 1 */
    uint32_t base;          /* Base address of IDT */
} __attribute__((packed)) idt_ptr_t;

/* -----------------------------------------------
 * Statically allocate 256 IDT entries
 * ----------------------------------------------- */
static idt_entry_t idt[256] __attribute__((aligned(8)));
static idt_ptr_t   idt_p;

/* C handler pointer table — one per vector */
static isr_handler_t interrupt_handlers[256];

/* Forward declarations of assembly stubs */
extern void isr0_stub(void);   extern void isr1_stub(void);
extern void isr2_stub(void);   extern void isr3_stub(void);
extern void isr4_stub(void);   extern void isr5_stub(void);
extern void isr6_stub(void);   extern void isr7_stub(void);
extern void isr8_stub(void);   extern void isr9_stub(void);
extern void isr10_stub(void);  extern void isr11_stub(void);
extern void isr12_stub(void);  extern void isr13_stub(void);
extern void isr14_stub(void);  extern void isr15_stub(void);
extern void isr16_stub(void);  extern void isr17_stub(void);
extern void isr18_stub(void);  extern void isr19_stub(void);
extern void isr20_stub(void);  extern void isr21_stub(void);
extern void isr22_stub(void);  extern void isr23_stub(void);
extern void isr24_stub(void);  extern void isr25_stub(void);
extern void isr26_stub(void);  extern void isr27_stub(void);
extern void isr28_stub(void);  extern void isr29_stub(void);
extern void isr30_stub(void);  extern void isr31_stub(void);

extern void irq0_stub(void);   extern void irq1_stub(void);
extern void irq2_stub(void);   extern void irq3_stub(void);
extern void irq4_stub(void);   extern void irq5_stub(void);
extern void irq6_stub(void);   extern void irq7_stub(void);
extern void irq8_stub(void);   extern void irq9_stub(void);
extern void irq10_stub(void);  extern void irq11_stub(void);
extern void irq12_stub(void);  extern void irq13_stub(void);
extern void irq14_stub(void);  extern void irq15_stub(void);

/* Pointers to assembly stubs for vectors 0-47 */
static void* stub_ptrs[48] = {
    isr0_stub,  isr1_stub,  isr2_stub,  isr3_stub,
    isr4_stub,  isr5_stub,  isr6_stub,  isr7_stub,
    isr8_stub,  isr9_stub,  isr10_stub, isr11_stub,
    isr12_stub, isr13_stub, isr14_stub, isr15_stub,
    isr16_stub, isr17_stub, isr18_stub, isr19_stub,
    isr20_stub, isr21_stub, isr22_stub, isr23_stub,
    isr24_stub, isr25_stub, isr26_stub, isr27_stub,
    isr28_stub, isr29_stub, isr30_stub, isr31_stub,
    irq0_stub,  irq1_stub,  irq2_stub,  irq3_stub,
    irq4_stub,  irq5_stub,  irq6_stub,  irq7_stub,
    irq8_stub,  irq9_stub,  irq10_stub, irq11_stub,
    irq12_stub, irq13_stub, irq14_stub, irq15_stub
};

/* Set one IDT entry */
static void idt_set_gate(uint8_t vector, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[vector].base_low  = base & 0xFFFF;
    idt[vector].base_high = (base >> 16) & 0xFFFF;
    idt[vector].sel       = sel;
    idt[vector].always0   = 0;
    idt[vector].flags     = flags;
}

/* -----------------------------------------------
 * PIC remapping
 * Remap the master PIC to IDT 0x20-0x27
 * Remap the slave  PIC to IDT 0x28-0x2F
 * This avoids conflict with CPU exceptions (0-31).
 * ----------------------------------------------- */
void pic_remap(void) {
    /* Save old masks */
    uint8_t mask_master = 0;
    uint8_t mask_slave  = 0;

    __asm__ volatile ("inb  %1, %0" : "=a"(mask_master) : "Nd"(0x21));
    __asm__ volatile ("inb  %1, %0" : "=a"(mask_slave)  : "Nd"(0xA1));

    /* ICW1: Start initialization sequence in cascade mode */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0x20));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x11), "Nd"((uint16_t)0xA0));

    /* ICW2: Remap IRQ base vectors */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x21)); /* Master: base 0x20 */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x28), "Nd"((uint16_t)0xA1)); /* Slave:  base 0x28 */

    /* ICW3: Tell master there's a slave at IRQ2; tell slave its cascade identity */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x04), "Nd"((uint16_t)0x21)); /* Master: slave on IRQ2 */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x02), "Nd"((uint16_t)0xA1)); /* Slave:  cascade ID 2 */

    /* ICW4: Set x86 mode */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x01), "Nd"((uint16_t)0xA1));

    /* Restore saved masks (all IRQs disabled initially; drivers will unmask) */
    __asm__ volatile ("outb %0, %1" : : "a"(mask_master), "Nd"((uint16_t)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"(mask_slave),  "Nd"((uint16_t)0xA1));

    write_serial("[PIC] Remapped master to 0x20, slave to 0x28\r\n");
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        /* Send EOI to slave PIC */
        __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0xA0));
    }
    /* Always send EOI to master PIC */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x20));
}

uint16_t pic_get_irq_mask(void) {
    uint8_t mask_master, mask_slave;
    __asm__ volatile ("inb %1, %0" : "=a"(mask_master) : "Nd"(0x21));
    __asm__ volatile ("inb %1, %0" : "=a"(mask_slave)  : "Nd"(0xA1));
    return ((uint16_t)mask_slave << 8) | mask_master;
}

void pic_set_irq_mask(uint16_t mask) {
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(mask & 0xFF)), "Nd"((uint16_t)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(mask >> 8)),    "Nd"((uint16_t)0xA1));
}

/* -----------------------------------------------
 * Register a C handler for a given interrupt vector
 * ----------------------------------------------- */
void register_interrupt_handler(uint8_t vector, isr_handler_t handler) {
    interrupt_handlers[vector] = handler;
}

/* -----------------------------------------------
 * C handler for CPU exceptions
 * Called from isr_stub_entry in interrupts.s
 * ----------------------------------------------- */
void exception_handler(registers_t* regs) {
    write_serial("\r\n!!! CPU EXCEPTION !!!\r\n");
    write_serial("Vector: 0x");
    write_serial_hex(regs->int_no);
    write_serial("\r\nError:  0x");
    write_serial_hex(regs->err_code);
    write_serial("\r\n");

    if (regs->int_no == 0x0E) {
        /* Page Fault — read CR2 */
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        write_serial("CR2 (fault addr): 0x");
        write_serial_hex(cr2);
        write_serial("\r\n");

        write_serial("Page fault ");
        if (!(regs->err_code & 0x1)) write_serial("(page not present) ");
        if (regs->err_code & 0x2)    write_serial("(write) ");
        else                         write_serial("(read) ");
        if (regs->err_code & 0x4)    write_serial("(user mode) ");
        if (regs->err_code & 0x8)    write_serial("(reserved bit overwritten) ");
        if (regs->err_code & 0x10)   write_serial("(instruction fetch) ");
        write_serial("\r\n");
    }

    write_serial("EIP: 0x");
    write_serial_hex(regs->eip);
    write_serial("  CS: 0x");
    write_serial_hex(regs->cs);
    write_serial("  EFLAGS: 0x");
    write_serial_hex(regs->eflags);
    write_serial("\r\n");

    /* Call panic() — draws BSOD, dumps all registers, then halts */
    panic("Unhandled CPU Exception", regs);
}

/* -----------------------------------------------
 * C handler for IRQs (hardware interrupts)
 * Called from isr_stub_entry in interrupts.s
 * ----------------------------------------------- */
void irq_handler(registers_t* regs) {
    /* Call the registered handler if one exists */
    if (interrupt_handlers[regs->int_no] != NULL) {
        interrupt_handlers[regs->int_no](regs);
    }

    /* Send End-of-Interrupt to the PIC */
    /* Convert vector number back to IRQ number */
    uint8_t irq = regs->int_no - 32;
    pic_send_eoi(irq);
}

/* -----------------------------------------------
 * Initialize the full IDT
 * ----------------------------------------------- */
void init_idt(void) {
    write_serial("[IDT] Initializing IDT...\r\n");

    /* Zero out the handler table */
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = NULL;
    }

    /* Set IDT pointer: 256 entries */
    idt_p.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_p.base  = (uint32_t)&idt;

    /* Install all exception and IRQ stubs into the IDT */
    for (int i = 0; i < 48; i++) {
        uint32_t stub_addr = (uint32_t)stub_ptrs[i];
        idt_set_gate(i, stub_addr, 0x08, 0x8E); /* Present, Ring 0, 32-bit Interrupt Gate */
    }

    /* Install syscall gate at vector 0x80 with DPL=3 (user-mode accessible) */
    /* 0xEE = Present, Ring 3, 32-bit Interrupt Gate */
    extern void isr128_handler(void);
    idt_set_gate(0x80, (uint32_t)isr128_handler, 0x08, 0xEE);

    /* Fill remaining vectors (49-255) with default handler */
    for (int i = 49; i < 256; i++) {
        if (i == 0x80) continue; /* Already set above */
        idt_set_gate(i, (uint32_t)stub_ptrs[0], 0x08, 0x8E);
    }

    /* Load the IDT */
    __asm__ volatile ("lidt %0" : : "m"(idt_p));

    /* Remap the PIC so IRQs don't conflict with CPU exceptions */
    pic_remap();

    write_serial("[IDT] IDT initialized successfully.\r\n");
}

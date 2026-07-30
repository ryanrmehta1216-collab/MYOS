#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/* IRQ numbers after PIC remap to IDT 0x20-0x2F */
#define IRQ0  32  /* PIT Timer */
#define IRQ1  33  /* PS/2 Keyboard */
#define IRQ2  34  /* Cascade (never used) */
#define IRQ3  35  /* COM2 */
#define IRQ4  36  /* COM1 */
#define IRQ5  37  /* LPT2 / Sound Card */
#define IRQ6  38  /* Floppy Disk */
#define IRQ7  39  /* LPT1 / Spurious */
#define IRQ8  40  /* CMOS RTC */
#define IRQ9  41  /* ACPI / OpenFirmware */
#define IRQ10 42  /* Free / SCSI */
#define IRQ11 43  /* Free / SCSI */
#define IRQ12 44  /* PS/2 Mouse */
#define IRQ13 45  /* FPU */
#define IRQ14 46  /* Primary ATA */
#define IRQ15 47  /* Secondary ATA */

/* CPU Exception Vectors */
#define EXC_DIVISION_ERROR      0x00
#define EXC_DEBUG               0x01
#define EXC_NMI                 0x02
#define EXC_BREAKPOINT          0x03
#define EXC_OVERFLOW            0x04
#define EXC_BOUND_RANGE         0x05
#define EXC_INVALID_OPCODE      0x06
#define EXC_DEVICE_NOT_AVAIL    0x07
#define EXC_DOUBLE_FAULT        0x08
#define EXC_COPROC_SEG_OVERRUN  0x09
#define EXC_INVALID_TSS         0x0A
#define EXC_SEG_NOT_PRESENT     0x0B
#define EXC_STACK_FAULT         0x0C
#define EXC_GENERAL_PROTECTION  0x0D
#define EXC_PAGE_FAULT          0x0E
#define EXC_X87_FPU             0x10
#define EXC_ALIGNMENT_CHECK     0x11
#define EXC_MACHINE_CHECK       0x12
#define EXC_SIMD_FPU            0x13
#define EXC_VIRT_EXCEPTION      0x14
#define EXC_SECURITY_EXCEPTION  0x1E

/* CPU exception handler — receives interrupted context */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, user_esp, user_ss;
} __attribute__((packed)) registers_t;

/* Interrupt handler function pointer type */
typedef void (*isr_handler_t)(registers_t* regs);

/* Initialize PIC (remap to 0x20-0x2F) */
void pic_remap(void);

/* Send End-of-Interrupt signal */
void pic_send_eoi(uint8_t irq);

/* Get combined IRQ mask from master + slave PICs */
uint16_t pic_get_irq_mask(void);

/* Set combined IRQ mask */
void pic_set_irq_mask(uint16_t mask);

/* Register a C handler for a given interrupt vector */
void register_interrupt_handler(uint8_t vector, isr_handler_t handler);

/* Initialize IDT with default handlers, then PIC remap */
void init_idt(void);

/* ISR stubs (defined in interrupts.s) */
extern void isr_stub_entry(void);

/* Exception handler stubs (defined in interrupts.s) */
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

/* IRQ handler stubs (defined in interrupts.s) */
extern void irq0_stub(void);   extern void irq1_stub(void);
extern void irq2_stub(void);   extern void irq3_stub(void);
extern void irq4_stub(void);   extern void irq5_stub(void);
extern void irq6_stub(void);   extern void irq7_stub(void);
extern void irq8_stub(void);   extern void irq9_stub(void);
extern void irq10_stub(void);  extern void irq11_stub(void);
extern void irq12_stub(void);  extern void irq13_stub(void);
extern void irq14_stub(void);  extern void irq15_stub(void);

#endif /* INTERRUPTS_H */

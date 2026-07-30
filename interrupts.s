.global isr_stub_entry

/* Exception stubs (0-31) */
.global isr0_stub,  isr1_stub,  isr2_stub,  isr3_stub
.global isr4_stub,  isr5_stub,  isr6_stub,  isr7_stub
.global isr8_stub,  isr9_stub,  isr10_stub, isr11_stub
.global isr12_stub, isr13_stub, isr14_stub, isr15_stub
.global isr16_stub, isr17_stub, isr18_stub, isr19_stub
.global isr20_stub, isr21_stub, isr22_stub, isr23_stub
.global isr24_stub, isr25_stub, isr26_stub, isr27_stub
.global isr28_stub, isr29_stub, isr30_stub, isr31_stub

/* IRQ stubs (32-47) */
.global irq0_stub,  irq1_stub,  irq2_stub,  irq3_stub
.global irq4_stub,  irq5_stub,  irq6_stub,  irq7_stub
.global irq8_stub,  irq9_stub,  irq10_stub, irq11_stub
.global irq12_stub, irq13_stub, irq14_stub, irq15_stub

.extern exception_handler
.extern irq_handler

.section .text
.code32

/* ============================================================
 * Master ISR handler: all exception/IRQ stubs converge here.
 * Saves full CPU context, calls C handler, restores, returns.
 * ============================================================ */
isr_stub_entry:
    /* Push the general-purpose registers */
    pusha
    /* Push the segment registers */
    push %ds
    push %es
    push %fs
    push %gs

    /* Set kernel data segments */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Push stack pointer (points to registers_t structure) */
    mov %esp, %eax
    push %eax

    /* Check if this is an IRQ (vector >= 32) by reading int_no from the stack.
     * The current ESP layout after above pushes is:
     *   ESP+0:  ptr to regs (just pushed)
     *   ESP+4:  gs
     *   ESP+8:  fs
     *   ESP+12: es
     *   ESP+16: ds
     *   ESP+20: edi (from pusha)
     *   ESP+24: esi
     *   ESP+28: ebp
     *   ESP+32: esp_old
     *   ESP+36: ebx
     *   ESP+40: edx
     *   ESP+44: ecx
     *   ESP+48: eax
     *   ESP+52: int_no
     *   ESP+56: err_code
     * We peek at [ESP+52] to decide which handler to call.
     */
    mov 52(%esp), %eax          /* Load int_no */
    cmp $32, %eax
    jl  .call_exception

    /* It's an IRQ (vector >= 32): call irq_handler(regs) */
    call irq_handler
    jmp .restore

.call_exception:
    /* It's an exception: call exception_handler(regs) */
    call exception_handler

.restore:
    /* Pop the regs pointer we pushed */
    pop %eax

    /* Restore segment registers */
    pop %gs
    pop %fs
    pop %es
    pop %ds

    /* Restore general-purpose registers */
    popa

    /* Clean up error code + interrupt number (8 bytes) pushed by stubs */
    add $8, %esp

    /* Return from interrupt */
    iret

/* ============================================================
 * Exception stubs
 * CPU pushes error code for exceptions 8, 10-14, 17, 30
 * For all others we push a dummy 0 so the stack layout is uniform.
 * ============================================================ */

/* No error code — push dummy 0 */
.macro isr_no_err vector
isr\vector\()_stub:
    push $0               /* dummy error code */
    push $\vector         /* interrupt number */
    jmp isr_stub_entry
.endm

/* CPU pushes error code automatically */
.macro isr_err vector
isr\vector\()_stub:
    /* error code already on stack by CPU */
    push $\vector         /* interrupt number */
    jmp isr_stub_entry
.endm

isr_no_err 0
isr_no_err 1
isr_no_err 2
isr_no_err 3
isr_no_err 4
isr_no_err 5
isr_no_err 6
isr_no_err 7
isr_err    8
isr_no_err 9
isr_err    10
isr_err    11
isr_err    12
isr_err    13
isr_err    14
isr_no_err 15
isr_no_err 16
isr_err    17
isr_no_err 18
isr_no_err 19
isr_no_err 20
isr_no_err 21
isr_no_err 22
isr_no_err 23
isr_no_err 24
isr_no_err 25
isr_no_err 26
isr_no_err 27
isr_no_err 28
isr_no_err 29
isr_err    30
isr_no_err 31

/* ============================================================
 * IRQ stubs (hardware interrupts)
 * These are identical to exception stubs with no error code
 * but use vectors 32-47.
 * ============================================================ */
.macro irq_stub irq_num vector
irq\irq_num\()_stub:
    push $0
    push $\vector
    jmp isr_stub_entry
.endm

irq_stub 0  32   /* IRQ0:  PIT Timer */
irq_stub 1  33   /* IRQ1:  PS/2 Keyboard */
irq_stub 2  34   /* IRQ2:  Cascade */
irq_stub 3  35   /* IRQ3:  COM2 */
irq_stub 4  36   /* IRQ4:  COM1 */
irq_stub 5  37   /* IRQ5:  LPT2 */
irq_stub 6  38   /* IRQ6:  Floppy */
irq_stub 7  39   /* IRQ7:  LPT1/Spurious */
irq_stub 8  40   /* IRQ8:  CMOS RTC */
irq_stub 9  41   /* IRQ9:  ACPI */
irq_stub 10 42   /* IRQ10: Free */
irq_stub 11 43   /* IRQ11: Free */
irq_stub 12 44   /* IRQ12: PS/2 Mouse */
irq_stub 13 45   /* IRQ13: FPU */
irq_stub 14 46   /* IRQ14: Primary ATA */
irq_stub 15 47   /* IRQ15: Secondary ATA */

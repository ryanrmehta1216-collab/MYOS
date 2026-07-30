.set MBALIGN,  1<<0
.set MEMINFO,  1<<1
.set VIDMODE,  1<<2
.set FLAGS,    MBALIGN | MEMINFO | VIDMODE
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0, 0, 0, 0, 0
.long 0
.long 1024
.long 768
.long 32

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.extern kernel_main

_start:
    mov $stack_top, %esp
    push %ebx        /* mbi: multiboot info structure pointer */
    push %eax        /* magic: multiboot magic value 0x2BADB002 */
    call kernel_main

.hang:
    hlt
    jmp .hang
    
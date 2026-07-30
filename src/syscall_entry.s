.global isr128_handler
.extern syscall_dispatcher

/* ============================================================
 * Syscall handler (interrupt 0x80)
 *
 * This ISR is registered with DPL=3 (ring 3 accessible).
 * When a user-mode process executes 'int $0x80':
 *   1. CPU switches to Ring 0 stack (via TSS.esp0)
 *   2. CPU pushes user SS, ESP, EFLAGS, CS, EIP on kernel stack
 *   3. We save remaining registers
 *   4. Call syscall_dispatcher(syscall_num, arg1, arg2, arg3)
 *   5. Restore registers and IRET back to user mode
 *
 * Register passing convention:
 *   EAX = syscall number
 *   EBX = arg1 (or security token)
 *   ECX = arg2
 *   EDX = arg3
 *
 * Return value goes in EAX (back to user program).
 * ============================================================ */
isr128_handler:
    /* Save all general-purpose registers */
    pushal

    /* Push arguments for syscall_dispatcher(eax, ebx, ecx, edx) */
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax

    /* Call the C dispatcher */
    call syscall_dispatcher

    /* Clean up 4 arguments (16 bytes) from stack */
    addl $16, %esp

    /* Overwrite the saved EAX (at pushal offset 28) with return value */
    movl %eax, 28(%esp)

    /* Restore general-purpose registers */
    popal

    /* Return to user mode (IRET restores CS, EIP, EFLAGS, SS, ESP) */
    iret

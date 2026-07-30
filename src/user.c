#include "user.h"
#include "serial.h"
#include "memory.h"

/* -----------------------------------------------
 * User Process Management
 *
 * This module manages user-mode (Ring 3) processes.
 * The kernel maintains a process table and provides
 * transition functions to enter user space.
 *
 * SECURITY MODEL:
 *   - User processes CANNOT modify GDT/IDT (Ring 0 only)
 *   - User processes CANNOT execute privileged instructions
 *   - User processes CANNOT access kernel memory directly
 *   - All kernel entry happens through the TSS stack switch
 * ----------------------------------------------- */

/* Static process table */
proc_t proc_table[MAX_USER_PROCS];
int    proc_count = 0;

/* Forward declarations for heap allocator */
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

/* Forward declaration for ring 3 assembly entry */
extern void switch_to_ring3_entry(void);

/* Initialize the user subsystem */
void init_user_subsystem(void) {
    write_serial("[USER] Initializing user mode subsystem...\r\n");

    for (int i = 0; i < MAX_USER_PROCS; i++) {
        proc_table[i].state = PROC_TERMINATED;
        proc_table[i].pid = 0;
    }
    proc_count = 0;

    write_serial("[USER] User subsystem initialized.\r\n");
}

/* Create a new user process.
 *
 * This allocates both a kernel stack (for interrupt/syscall entry)
 * and a user stack (for the process itself). The process starts
 * executing at 'entry_point' in Ring 3.
 *
 * Returns: PID on success, -1 on failure.
 */
int create_user_process(const char* name, void* entry_point) {
    if (proc_count >= MAX_USER_PROCS) {
        write_serial("[USER] ERROR: Process table full!\r\n");
        return -1;
    }

    /* Find an empty slot */
    int slot = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (proc_table[i].state == PROC_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    proc_t* proc = &proc_table[slot];
    proc->pid = slot + 1;          /* PID 0 reserved for kernel idle */
    proc->state = PROC_CREATED;
    proc->entry_point = (uint32_t)entry_point;
    proc->cpu_time = 0;

    /* Copy process name */
    int i = 0;
    while (name[i] != '\0' && i < 31) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';

    /* Allocate kernel stack (for ring 0 entry via TSS) */
    /* This stack is used when the process makes a syscall or gets interrupted */
    uint32_t* kstack = (uint32_t*)(uint32_t)kmalloc(USER_STACK_SIZE);
    if (!kstack) {
        write_serial("[USER] ERROR: Failed to allocate kernel stack!\r\n");
        return -1;
    }
    proc->kernel_stack = (uint32_t)kstack + USER_STACK_SIZE;
    /* Pre-fault kernel stack by touching pages (already pre-allocated in heap) */

    /* Allocate user stack (for the process itself in ring 3) */
    uint32_t* ustack = (uint32_t*)(uint32_t)kmalloc(USER_STACK_SIZE);
    if (!ustack) {
        write_serial("[USER] ERROR: Failed to allocate user stack!\r\n");
        kfree(kstack);
        return -1;
    }
    proc->user_stack = (uint32_t)ustack + USER_STACK_SIZE;

    proc->state = PROC_READY;
    proc_count++;

    write_serial("[USER] Created process \"");
    write_serial(proc->name);
    write_serial("\" PID=");
    write_serial_hex(proc->pid);
    write_serial(" entry=0x");
    write_serial_hex(proc->entry_point);
    write_serial(" usr_stack=0x");
    write_serial_hex(proc->user_stack);
    write_serial("\r\n");

    return proc->pid;
}

/* Switch to Ring 3 user mode.
 *
 * This function sets up the stack for an IRET that will
 * transition the CPU from Ring 0 (kernel) to Ring 3 (user).
 *
 * x86 IRET stack layout (top to bottom):
 *   [ESP]     = user SS       (0x23 = Ring 3 data segment)
 *   [ESP+4]   = user ESP      (user stack pointer)
 *   [ESP+8]   = user EFLAGS   (IF=1 to enable interrupts)
 *   [ESP+12]  = user CS       (0x1B = Ring 3 code segment)
 *   [ESP+16]  = user EIP      (entry point)
 *
 * After IRET, the CPU is in Ring 3 with full interrupt capability.
 * The ONLY way back to Ring 0 is via an interrupt or syscall.
 *
 * NOTE: This function never returns. It runs the process until
 *       it makes a syscall or gets preempted.
 */
void __attribute__((noreturn)) switch_to_ring3(uint32_t entry, uint32_t user_stack) {
    write_serial("[USER] Entering Ring 3 at 0x");
    write_serial_hex(entry);
    write_serial("\r\n");

    /* Disable interrupts during transition */
    __asm__ volatile("cli");

    /* Build the IRET frame on the kernel stack.
     *
     * We push values in reverse order so the stack layout
     * matches what IRET expects when it pops them.
     */

    /* Step 1: Push user stack segment selector (Ring 3 data: 0x23) */
    __asm__ volatile("pushl $0x23");

    /* Step 2: Push user stack pointer */
    __asm__ volatile("pushl %0" : : "r"(user_stack));

    /* Step 3: Push EFLAGS with IF (interrupt flag) set */
    __asm__ volatile("pushfl");
    __asm__ volatile("orl $0x200, (%esp)");  /* Set IF bit (bit 9) */
    __asm__ volatile("andl $~0x4000, (%esp)"); /* Clear NT (nested task) */

    /* Step 4: Push user code segment selector (Ring 3 code: 0x1B) */
    __asm__ volatile("pushl $0x1B");

    /* Step 5: Push user entry point */
    __asm__ volatile("pushl %0" : : "r"(entry));

    /* Execute IRET to jump to Ring 3 */
    __asm__ volatile("iret");

    /* Never reached */
    for (;;) __asm__ volatile("hlt");
}

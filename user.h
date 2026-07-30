#ifndef USER_H
#define USER_H

#include <stdint.h>

/* -----------------------------------------------
 * Ring 3 User Process Management
 *
 * Architecture:
 *   User processes run at Ring 3 (CPL=3) with strict
 *   hardware isolation from the kernel (Ring 0).
 *   
 *   Transitions between rings happen ONLY through:
 *   - Interrupts (hardware or software: int 0x80 syscalls)
 *   - IRET (return from interrupt to user mode)
 *   
 *   The TSS.esp0 provides the kernel stack for ring 0
 *   entry when an interrupt/syscall occurs in user mode.
 * ----------------------------------------------- */

/* User stack size: 8 KB per process */
#define USER_STACK_SIZE 8192

/* Maximum number of user processes */
#define MAX_USER_PROCS 16

/* User process states */
typedef enum {
    PROC_CREATED  = 0,
    PROC_READY    = 1,
    PROC_RUNNING  = 2,
    PROC_BLOCKED  = 3,
    PROC_TERMINATED = 4
} proc_state_t;

/* User process control block */
typedef struct {
    uint32_t    pid;            /* Process ID */
    proc_state_t state;         /* Current state */
    uint32_t    entry_point;    /* EIP to start execution */
    uint32_t    kernel_stack;   /* Kernel stack top (TSS.esp0 will point here) */
    uint32_t    user_stack;     /* User stack top */
    uint32_t    page_dir;       /* Physical address of process page directory */
    char        name[32];       /* Process name */
    uint32_t    cpu_time;       /* Accumulated CPU ticks */
} proc_t;

/* Global process table */
extern proc_t proc_table[MAX_USER_PROCS];
extern int    proc_count;

/* Initialize user mode subsystem */
void init_user_subsystem(void);

/* Create a new user process that runs at Ring 3 */
int create_user_process(const char* name, void* entry_point);

/* Switch to user mode and never return (called once per process) */
void __attribute__((noreturn)) switch_to_ring3(uint32_t entry, uint32_t user_stack);

/* System call numbers */
#define SYS_WRITE       0x01
#define SYS_READ        0x02
#define SYS_GETPID      0x03
#define SYS_SLEEP       0x04
#define SYS_DEBUG       0x05

#endif /* USER_H */

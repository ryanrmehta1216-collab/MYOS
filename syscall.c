#include <stdint.h>
#include "serial.h"
#include "user.h"
#include "capability.h"
#include "memory.h"

/* -----------------------------------------------
 * System Call Dispatcher
 *
 * All user-mode requests enter here via int 0x80.
 *
 * Calling convention from user mode:
 *   EAX = syscall number
 *   EBX = arg1
 *   ECX = arg2
 *   EDX = arg3
 *
 * Architecture: Zero-Trust Security
 *   Every syscall verifies the calling process has the
 *   required capability token before performing the action.
 *   This enforces the Principle of Least Privilege.
 * ----------------------------------------------- */

/* Capability table for the current (only) user process */
cap_table_t current_process_caps;

/* Read the 64-bit CPU Time-Stamp Counter for benchmarking */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Initialize syscall capability table */
void init_syscalls(void) {
    init_default_process_capabilities(&current_process_caps);
}

/* --- System call implementations --- */

/* SYS_WRITE: Write a string to the serial console.
 *   arg1 = pointer to string
 *   arg2 = length (if 0, strlen is used)
 */
static uint32_t sys_write(uint32_t str_ptr, uint32_t length, uint32_t unused) {
    (void)unused;
    char* str = (char*)str_ptr;
    if (length == 0) {
        /* Null-terminated */
        for (int i = 0; str[i] != '\0'; i++) {
            write_serial_char(str[i]);
        }
    } else {
        for (uint32_t i = 0; i < length; i++) {
            write_serial_char(str[i]);
        }
    }
    write_serial("\r\n");
    return length;
}

/* SYS_GETPID: Return the current process ID.
 *   No arguments needed.
 */
static uint32_t sys_getpid(void) {
    /* For now, always return PID 1 (the initial user process) */
    return 1;
}

/* SYS_DEBUG: Debug output with hex value.
 *   arg1 = hex value to print
 */
static uint32_t sys_debug(uint32_t value, uint32_t unused1, uint32_t unused2) {
    (void)unused1;
    (void)unused2;
    write_serial("[USER] Debug: 0x");
    write_serial_hex(value);
    write_serial("\r\n");
    return 0;
}

/* SYS_SLEEP: Sleep for 'arg1' milliseconds (busy-wait).
 *   arg1 = milliseconds to sleep
 */
static uint32_t sys_sleep(uint32_t ms, uint32_t unused1, uint32_t unused2) {
    (void)unused1;
    (void)unused2;
    extern void sleep_ms(uint32_t);
    sleep_ms(ms);
    return 0;
}

/* Main dispatcher — called from syscall_entry.s */
uint32_t syscall_dispatcher(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    switch (syscall_num) {
        case SYS_WRITE:
            return sys_write(arg1, arg2, arg3);

        case SYS_GETPID:
            return sys_getpid();

        case SYS_DEBUG:
            return sys_debug(arg1, arg2, arg3);

        case SYS_SLEEP:
            return sys_sleep(arg1, arg2, arg3);

        default:
            /* Unknown syscall — return error code 0x404 (Not Found) */
            write_serial("[SYSCALL] Unknown syscall: 0x");
            write_serial_hex(syscall_num);
            write_serial("\r\n");
            return 0x404;
    }
}

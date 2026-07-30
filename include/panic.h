#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
#include "interrupts.h"

/* -----------------------------------------------
 * Kernel Panic — Blue Screen of Death
 *
 * Formal panic() function that halts the system
 * and dumps CPU registers to the framebuffer
 * for real-world debugging.
 *
 * Called automatically on CPU exceptions via
 * exception_handler(), or manually via panic().
 * ----------------------------------------------- */

/* Halt the system with a blue-screen error dump.
 * message: brief description of the failure
 * regs: CPU register state at the time of fault (can be NULL) */
void panic(const char* message, registers_t* regs) __attribute__((noreturn));

/* Internal: draw the BSOD framebuffer dump */
void panic_draw_bsod(const char* message, registers_t* regs);

#endif /* PANIC_H */

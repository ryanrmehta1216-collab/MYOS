#include <stdint.h>
#include "interrupts.h"
#include "scheduler.h"

/* -----------------------------------------------
 * PIT (Programmable Interval Timer)
 * Generates IRQ0 at a configurable frequency.
 *
 * Integrated with the Inertia Scheduler: on each tick,
 * the timer handler calls scheduler_tick() to determine
 * if a context switch is needed.
 *
 * For preemptive multitasking, the timer handler can
 * modify the saved register context (passed as regs)
 * to redirect execution to the next scheduled process.
 * ----------------------------------------------- */
#define PIT_BASE_FREQ 1193180  /* 1.193180 MHz */

volatile uint32_t timer_ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Initialize PIT with desired frequency in Hz */
void init_pit(uint32_t frequency) {
    if (frequency == 0) frequency = 100; /* Default 100 Hz */
    uint32_t divisor = PIT_BASE_FREQ / frequency;

    /* Command: channel 0, lobyte/hibyte, rate generator */
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);        /* Low byte */
    outb(0x40, (divisor >> 8) & 0xFF); /* High byte */
}

/* IRQ0 handler (timer tick) — called from irq_handler via registered handler.
 *
 * Increments the tick counter and triggers the scheduler.
 * The scheduler decides if a context switch is needed.
 *
 * For true preemption, this handler could modify regs->eip
 * and regs->esp to redirect the IRET return to a different
 * process's context. This is stored per-process in task.c.
 */
static void timer_handler_c(registers_t* regs) {
    (void)regs;
    timer_ticks++;

    /* Call the Inertia Scheduler on each tick */
    scheduler_tick();
}

void init_timer(void) {
    init_pit(100);  /* 100 Hz -> 10ms per tick */
    register_interrupt_handler(IRQ0, timer_handler_c);
}

uint32_t get_ticks(void) {
    return timer_ticks;
}

/* Busy-wait for approximately 'ms' milliseconds */
void sleep_ms(uint32_t ms) {
    uint32_t start = timer_ticks;
    /* Each tick is ~10ms at 100 Hz */
    uint32_t ticks_to_wait = (ms + 9) / 10;
    while ((timer_ticks - start) < ticks_to_wait) {
        __asm__ volatile ("pause");
    }
}

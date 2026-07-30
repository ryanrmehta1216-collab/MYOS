#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

/* -----------------------------------------------
 * Inertia Scheduler
 *
 * A preemptive, power-aware scheduler that minimizes
 * energy consumption by aggressively HLT-ing the CPU
 * when no process is ready to run.
 *
 * Algorithm: "Inertia Scheduling"
 *   Instead of busy-waiting in an idle loop, the scheduler
 *   tracks process activity and puts the CPU to sleep (HLT)
 *   when all processes are blocked or waiting. The next
 *   hardware interrupt (PIT timer) wakes the CPU back up.
 *
 *   "Inertia" = tendency to remain idle unless actively
 *   pushed to work. This is the opposite of a busy-loop
 *   scheduler that polls constantly.
 *
 * Statistics:
 *   - idle_ticks:   Number of timer ticks spent in HLT
 *   - active_ticks: Number of timer ticks doing work
 *   - context_switches: Total preemptions performed
 *
 *   CPU idle % = idle_ticks / (idle_ticks + active_ticks) * 100
 *   This is a direct measure of power saved vs a busy-loop scheduler.
 * ----------------------------------------------- */

/* Scheduler statistics for telemetry */
typedef struct {
    volatile uint32_t idle_ticks;
    volatile uint32_t active_ticks;
    volatile uint32_t context_switches;
    volatile uint32_t total_ticks;
} scheduler_stats_t;

extern scheduler_stats_t scheduler_stats;

/* Initialize the scheduler */
void init_scheduler(void);

/* Called on each PIT tick to potentially preempt the current task */
void scheduler_tick(void);

/* Get a pointer to scheduler stats for the telemetry dashboard */
scheduler_stats_t* get_scheduler_stats(void);

/* Idle task: HLTs the CPU. Woken by next interrupt. */
void idle_task(void) __attribute__((noreturn));

#endif /* SCHEDULER_H */

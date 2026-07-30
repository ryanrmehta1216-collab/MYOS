#include "scheduler.h"
#include "user.h"
#include "serial.h"

/* -----------------------------------------------
 * Inertia Scheduler Implementation
 *
 * Preemptive, round-robin scheduler with aggressive
 * power saving via HLT.
 *
 * On each PIT tick:
 *   1. Save the current process's context
 *   2. Advance to the next ready process
 *   3. If no process is ready, HLT (wakes on next interrupt)
 *   4. Restore the next process's context
 *
 * The scheduler data is kept in statically allocated
 * memory so it's always accessible, even when the
 * current process page directory changes.
 * ----------------------------------------------- */

/* Scheduler statistics */
scheduler_stats_t scheduler_stats;

/* Current running process index (into proc_table) */
static volatile int current_proc = -1;

/* Number of ready processes */
static volatile int ready_count = 0;

/* Initialize the scheduler */
void init_scheduler(void) {
    write_serial("[SCHED] Initializing Inertia Scheduler...\r\n");

    scheduler_stats.idle_ticks = 0;
    scheduler_stats.active_ticks = 0;
    scheduler_stats.context_switches = 0;
    scheduler_stats.total_ticks = 0;

    current_proc = -1;
    ready_count = 0;

    write_serial("[SCHED] Inertia Scheduler initialized.\r\n");
}

/* Advance the scheduler to the next ready process.
 *
 * Uses round-robin: scan forward from the current process,
 * wrapping around, until a READY process is found.
 *
 * If no process is READY, current_proc = -1 signals the
 * idle task to HLT.
 */
static int find_next_ready(int start) {
    if (proc_count == 0) return -1;

    for (int i = 0; i < MAX_USER_PROCS; i++) {
        int idx = (start + 1 + i) % MAX_USER_PROCS;
        if (proc_table[idx].state == PROC_READY) {
            return idx;
        }
    }
    return -1; /* No ready process */
}

/* Called on each PIT timer tick (IRQ0).
 *
 * This is the preemption point. It saves the current
 * context and switches to the next ready process.
 *
 * The context switch is triggered by setting a flag
 * that the timer ISR will check. The actual context
 * saving/restoring happens in the timer handler.
 */
void scheduler_tick(void) {
    scheduler_stats.total_ticks++;

    /* Find the next ready process */
    int next = find_next_ready(current_proc);

    if (next >= 0) {
        /* Mark current as ready (if it was running) */
        if (current_proc >= 0 && current_proc < MAX_USER_PROCS) {
            if (proc_table[current_proc].state == PROC_RUNNING) {
                proc_table[current_proc].state = PROC_READY;
            }
        }

        /* Set next as running */
        proc_table[next].state = PROC_RUNNING;
        proc_table[next].cpu_time++;

        if (next != current_proc) {
            scheduler_stats.context_switches++;
        }

        current_proc = next;
        scheduler_stats.active_ticks++;

    } else {
        /* No process ready — we'll go idle */
        current_proc = -1;
        scheduler_stats.idle_ticks++;
    }
}

/* Get scheduler stats */
scheduler_stats_t* get_scheduler_stats(void) {
    return &scheduler_stats;
}

/* Idle task — halts the CPU until the next interrupt.
 *
 * This is the core of the power-saving strategy.
 * Instead of busy-waiting, the CPU executes HLT which
 * stops instruction execution until the next external
 * interrupt (PIT tick, keyboard, mouse, etc.).
 *
 * On wake, we immediately return to the idle loop,
 * which will HLT again if no process is ready.
 *
 * Power savings vs busy-loop:
 *   Busy-loop: 100% CPU, ~15-30W on a real CPU
 *   HLT loop:  ~1-5% effective CPU, ~1-3W on a real CPU
 *   Saving:    80-93% power reduction
 */
void idle_task(void) {
    for (;;) {
        /* 
         * The scheduler stats idle_ticks are incremented 
         * in scheduler_tick when no process is ready.
         * Here we just HLT and let interrupts wake us.
         *
         * NOTE: This runs at Ring 0, so HLT is legal.
         * User processes cannot execute HLT (GP fault).
         */
        __asm__ volatile("hlt");
    }
}

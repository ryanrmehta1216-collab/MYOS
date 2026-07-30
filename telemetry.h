#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

/* -----------------------------------------------
 * Live Kernel Telemetry Dashboard (Phase 8)
 *
 * Overlays real-time performance metrics on the
 * desktop compositor. Tracks and displays:
 *
 *   - CPU idle % (Inertia Scheduler power saving)
 *   - Context switch rate
 *   - Aegis ECC faults detected/corrected
 *   - Aura-Net frames sent/received
 *   - System uptime (timer ticks)
 *
 * All data is read from volatile counters in the
 * scheduler, aegis, and aura_net modules.
 * ----------------------------------------------- */

/* Telemetry display state */
typedef struct {
    uint8_t  visible;          /* Toggle overlay visibility */
    uint32_t update_interval;  /* Ticks between refresh */
    uint32_t last_update;      /* Last tick when updated */
    uint32_t uptime_ticks;     /* System uptime in ticks */
    uint32_t cpu_idle_pct;     /* CPU idle percentage (0-100) */
    uint32_t ctx_switches;     /* Total context switches */
    uint32_t ecc_faults;       /* Aegis ECC faults detected */
    uint32_t ecc_corrected;    /* Aegis ECC faults corrected */
    uint32_t net_sent;         /* Aura-Net frames sent */
    uint32_t net_received;     /* Aura-Net frames received */
    char     ip_str[16];       /* Our IP address string */
} telemetry_data_t;

/* Initialize telemetry system */
void init_telemetry(void);

/* Get current telemetry data */
telemetry_data_t* get_telemetry_data(void);

/* Refresh telemetry counters from subsystems */
void telemetry_refresh(void);

/* Render the telemetry overlay on the backbuffer */
void telemetry_render(void);

#endif /* TELEMETRY_H */

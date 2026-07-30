#include "telemetry.h"
#include "scheduler.h"
#include "aegis.h"
#include "aura_net.h"
#include "serial.h"

/* -----------------------------------------------
 * Live Telemetry Dashboard
 *
 * Aggregates data from the Inertia Scheduler,
 * Aegis Memory Protection, and Aura-Net networking
 * into a unified statistics structure.
 *
 * This data is rendered as an overlay by the
 * desktop compositor (desktop.c).
 * ----------------------------------------------- */

/* Global telemetry data */
static telemetry_data_t telemetry;

/* Initialize telemetry */
void init_telemetry(void) {
    write_serial("[TELEM] Initializing live telemetry dashboard...\r\n");

    telemetry.visible = 1;
    telemetry.update_interval = 10;  /* Update every 10 ticks (~100ms) */
    telemetry.last_update = 0;
    telemetry.uptime_ticks = 0;
    telemetry.cpu_idle_pct = 0;
    telemetry.ctx_switches = 0;
    telemetry.ecc_faults = 0;
    telemetry.ecc_corrected = 0;
    telemetry.net_sent = 0;
    telemetry.net_received = 0;

    /* Build IP string */
    telemetry.ip_str[0] = '1';
    telemetry.ip_str[1] = '0';
    telemetry.ip_str[2] = '.';
    telemetry.ip_str[3] = '0';
    telemetry.ip_str[4] = '.';
    telemetry.ip_str[5] = '0';
    telemetry.ip_str[6] = '.';
    telemetry.ip_str[7] = '1';
    telemetry.ip_str[8] = '\0';

    write_serial("[TELEM] Telemetry dashboard initialized.\r\n");
}

/* Get telemetry data pointer */
telemetry_data_t* get_telemetry_data(void) {
    return &telemetry;
}

/* Refresh telemetry counters from all subsystems */
void telemetry_refresh(void) {
    extern uint32_t timer_ticks;
    telemetry.uptime_ticks = timer_ticks;

    /* Get scheduler stats */
    scheduler_stats_t* sched = get_scheduler_stats();
    uint32_t total = sched->active_ticks + sched->idle_ticks;
    if (total > 0) {
        telemetry.cpu_idle_pct = (sched->idle_ticks * 100) / total;
    } else {
        telemetry.cpu_idle_pct = 100; /* Default: idle */
    }
    telemetry.ctx_switches = sched->context_switches;

    /* Get Aegis stats */
    aegis_stats_t* aegis = get_aegis_stats();
    telemetry.ecc_faults = aegis->faults_detected;
    telemetry.ecc_corrected = aegis->faults_corrected;

    /* Get Aura-Net stats */
    aura_stats_t* aura = get_aura_stats();
    telemetry.net_sent = aura->frames_sent;
    telemetry.net_received = aura->frames_received;
}

/* Render the telemetry overlay on the backbuffer.
 * This is called from desktop_task() after all windows
 * have been drawn, but before the mouse cursor.
 *
 * Uses gfx.c drawing functions via extern declarations. */
void telemetry_render(void) {
    if (!telemetry.visible) return;

    /* Refresh data */
    telemetry_refresh();

    /* Extern declarations for gfx.c functions */
    extern void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    extern void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color);

    /* Draw translucent overlay background (semi-transparent dark block) */
    /* We use the backbuffer directly - x=0, y=0, width=270, height=170 */
    /* In a real system with alpha, we'd blend. Here we use a dark solid color. */
    draw_rect(0, 0, 270, 175, 0x1A1A2E);  /* Dark panel */
    draw_rect(0, 0, 270, 1, 0xB4BEFE);    /* Top accent line */
    draw_rect(0, 175, 270, 1, 0xB4BEFE);  /* Bottom accent line */
    draw_rect(0, 0, 1, 175, 0xB4BEFE);    /* Left accent line */

    /* Title */
    draw_string(6, 4, "MYOS Kernel Telemetry", 0xB4BEFE);

    /* System uptime */
    char line[64];
    line[0] = 'U';
    line[1] = 'p';
    line[2] = 't';
    line[3] = 'i';
    line[4] = 'm';
    line[5] = 'e';
    line[6] = ':';
    line[7] = ' ';
    line[8] = '0' + (telemetry.uptime_ticks / 10000) % 10;
    line[9] = '0' + (telemetry.uptime_ticks / 1000) % 10;
    line[10] = '.';
    line[11] = '0' + (telemetry.uptime_ticks / 100) % 10;
    line[12] = ' ';
    line[13] = 's';
    line[14] = '\0';
    draw_string(6, 16, line, 0xA6ADC8);

    /* CPU Idle % */
    line[0] = 'C';
    line[1] = 'P';
    line[2] = 'U';
    line[3] = ' ';
    line[4] = 'I';
    line[5] = 'd';
    line[6] = 'l';
    line[7] = 'e';
    line[8] = ':';
    line[9] = ' ';
    line[10] = '0' + (telemetry.cpu_idle_pct / 10) % 10;
    line[11] = '0' + telemetry.cpu_idle_pct % 10;
    line[12] = '%';
    line[13] = '\0';
    draw_string(6, 28, line, 0xA6E3A1);

    /* Context switches */
    line[0] = 'C';
    line[1] = 't';
    line[2] = 'x';
    line[3] = ' ';
    line[4] = 'S';
    line[5] = 'w';
    line[6] = ':';
    line[7] = ' ';
    line[8] = '0' + (telemetry.ctx_switches / 1000) % 10;
    line[9] = '0' + (telemetry.ctx_switches / 100) % 10;
    line[10] = '0' + (telemetry.ctx_switches / 10) % 10;
    line[11] = '0' + telemetry.ctx_switches % 10;
    line[12] = '\0';
    draw_string(6, 40, line, 0x89B4FA);

    /* Aegis ECC */
    line[0] = 'E';
    line[1] = 'C';
    line[2] = 'C';
    line[3] = ' ';
    line[4] = 'F';
    line[5] = 'a';
    line[6] = 'u';
    line[7] = 'l';
    line[8] = 't';
    line[9] = 's';
    line[10] = ':';
    line[11] = ' ';
    line[12] = '0' + telemetry.ecc_faults % 10;
    line[13] = ' ';
    line[14] = 'C';
    line[15] = 'o';
    line[16] = 'r';
    line[17] = 'r';
    line[18] = ':';
    line[19] = ' ';
    line[20] = '0' + telemetry.ecc_corrected % 10;
    line[21] = '\0';
    draw_string(6, 52, line, 0xF38BA8);

    /* Aura-Net frames */
    line[0] = 'N';
    line[1] = 'e';
    line[2] = 't';
    line[3] = ' ';
    line[4] = 'T';
    line[5] = 'X';
    line[6] = ':';
    line[7] = ' ';
    line[8] = '0' + (telemetry.net_sent / 100) % 10;
    line[9] = '0' + (telemetry.net_sent / 10) % 10;
    line[10] = '0' + telemetry.net_sent % 10;
    line[11] = ' ';
    line[12] = 'R';
    line[13] = 'X';
    line[14] = ':';
    line[15] = ' ';
    line[16] = '0' + (telemetry.net_received / 100) % 10;
    line[17] = '0' + (telemetry.net_received / 10) % 10;
    line[18] = '0' + telemetry.net_received % 10;
    line[19] = '\0';
    draw_string(6, 64, line, 0xFAB387);

    /* IP Address */
    line[0] = 'I';
    line[1] = 'P';
    line[2] = ':';
    line[3] = ' ';
    int i = 4;
    int j = 0;
    while (telemetry.ip_str[j] != '\0' && i < 20) {
        line[i++] = telemetry.ip_str[j++];
    }
    line[i] = '\0';
    draw_string(6, 76, line, 0xCBA6F7);

    /* Status separator */
    draw_string(6, 88, "--- Status ---", 0x45475A);

    /* Quarantined pages */
    aegis_stats_t* aegis = get_aegis_stats();
    line[0] = 'Q';
    line[1] = 'u';
    line[2] = 'a';
    line[3] = 'r';
    line[4] = '.';
    line[5] = ' ';
    line[6] = 'P';
    line[7] = 'a';
    line[8] = 'g';
    line[9] = 'e';
    line[10] = 's';
    line[11] = ':';
    line[12] = ' ';
    line[13] = '0' + aegis->quarantined_pages % 10;
    line[14] = '\0';
    draw_string(6, 100, line, 0xF38BA8);

    /* Power savings indicator */
    draw_string(6, 112, "Power: HLT Idle", 0xA6E3A1);

    /* Page count */
    line[0] = 'F';
    line[1] = 'r';
    line[2] = 'e';
    line[3] = 'e';
    line[4] = ' ';
    line[5] = 'P';
    line[6] = 'a';
    line[7] = 'g';
    line[8] = 'e';
    line[9] = 's';
    line[10] = ':';
    line[11] = ' ';
    line[12] = ' ';
    line[13] = ' ';
    draw_string(6, 124, line, 0xA6ADC8);

    /* Heartbeats */
    aura_stats_t* aura = get_aura_stats();
    line[0] = 'H';
    line[1] = 'e';
    line[2] = 'a';
    line[3] = 'r';
    line[4] = 't';
    line[5] = ' ';
    line[6] = 'I';
    line[7] = 'n';
    line[8] = 't';
    line[9] = 'e';
    line[10] = 'r';
    line[11] = 'v';
    line[12] = ':';
    line[13] = ' ';
    line[14] = '5';
    line[15] = 's';
    line[16] = ' ';
    line[17] = '(';
    line[18] = '0' + (aura->heartbeats_sent / 100) % 10;
    line[19] = '0' + (aura->heartbeats_sent / 10) % 10;
    line[20] = '0' + aura->heartbeats_sent % 10;
    line[21] = '/';
    line[22] = '0' + (aura->heartbeats_received / 100) % 10;
    line[23] = '0' + (aura->heartbeats_received / 10) % 10;
    line[24] = '0' + aura->heartbeats_received % 10;
    line[25] = ')';
    line[26] = '\0';
    draw_string(6, 136, line, 0xFAB387);

    /* System mode indicator */
    draw_string(6, 148, "Mode: Protected 32-bit", 0x89B4FA);

    /* Architecture badge */
    draw_string(6, 160, "x86 Ring 0/3 | Paging | HLT", 0x45475A);
}

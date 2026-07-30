#ifndef AEGIS_H
#define AEGIS_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------
 * Aegis Memory Protection (Phase 5)
 *
 * Software-defined ECC for physical memory pages.
 * Every allocated page gets a CRC32 checksum stored
 * in a shadow map. On deallocation or scrub scan,
 * the checksum is verified. Mismatches trigger:
 *   1. Error logging
 *   2. Attempted single-bit correction
 *   3. Page quarantine (prevent re-allocation)
 *
 * The quarantine table stores up to MAX_QUARANTINED
 * bad page frame addresses. These are never re-used.
 * ----------------------------------------------- */

/* Maximum quarantined page frames */
#define MAX_QUARANTINED 64

/* Aegis statistics for telemetry */
typedef struct {
    volatile uint32_t pages_protected;   /* Pages with active checksums */
    volatile uint32_t faults_detected;   /* Total checksum mismatches */
    volatile uint32_t faults_corrected;  /* Corrected single-bit errors */
    volatile uint32_t quarantined_pages; /* Pages permanently quarantined */
} aegis_stats_t;

extern aegis_stats_t aegis_stats;

/* CRC32 lookup table (pre-computed) */
extern uint32_t crc32_table[256];

/* Initialize the Aegis subsystem */
void aegis_init(void);

/* Compute CRC32 for a block of memory */
uint32_t aegis_crc32(const uint8_t* data, size_t length);

/* Set ECC checksum for a physical page frame */
void aegis_protect_page(void* phys_addr);

/* Verify ECC checksum for a physical page frame.
 * Returns 0 if intact, 1 if corrected, 2 if uncorrectable.
 * If uncorrectable, the page is quarantined. */
int aegis_verify_page(void* phys_addr);

/* Remove protection (called when page is freed) */
void aegis_unprotect_page(void* phys_addr);

/* Scan all protected pages for bit rot. Returns number of faults found. */
int aegis_scrub_scan(void);

/* Check if a physical address is quarantined */
int aegis_is_quarantined(void* phys_addr);

/* Check quarantine by physical address value (for PMM visualizer) */
uint32_t aegis_quarantine_check(uint32_t phys_addr);

/* Get Aegis stats */
aegis_stats_t* get_aegis_stats(void);

#endif /* AEGIS_H */

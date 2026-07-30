#include "aegis.h"
#include "pmm.h"
#include "serial.h"
#include "memory.h"

/* -----------------------------------------------
 * Aegis Memory Protection
 *
 * Uses a simple parity-based error detection scheme:
 * Each 4KB page gets a 32-bit CRC32 checksum stored
 * in a shadow array. On verification, mismatches are
 * logged and the page is quarantined.
 *
 * Rather than attempting CRC-based bit correction
 * (which is mathematically unsound - CRC is not an
 * error-correcting code), Aegis instead:
 *   1. Detects mismatches via CRC
 *   2. Logs the fault
 *   3. Quarantines the bad page
 *   4. A background scrub scan periodically checks
 *      all protected pages for silent corruption
 *
 * For true error correction, a hardware ECC memory
 * controller would be needed. Our software layer
 * provides detection and isolation.
 * ----------------------------------------------- */

/* Aegis statistics */
aegis_stats_t aegis_stats;

/* CRC32 lookup table (polynomial 0xEDB88320) */
uint32_t crc32_table[256];

/* Shadow checksum array: one 32-bit CRC per managed page frame */
#define AEGIS_MAX_PAGES 32768
static uint32_t aegis_shadow[AEGIS_MAX_PAGES];

/* Quarantine list: addresses of bad pages */
static uint32_t quarantine_list[MAX_QUARANTINED];
static int quarantine_count = 0;

/* Build the CRC32 lookup table */
static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
}

/* Compute CRC32 over a memory region */
uint32_t aegis_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
    }
    return crc ^ 0xFFFFFFFF;
}

/* Initialize Aegis */
void aegis_init(void) {
    write_serial("[AEGIS] Initializing Aegis Memory Protection...\r\n");

    crc32_init_table();

    aegis_stats.pages_protected = 0;
    aegis_stats.faults_detected = 0;
    aegis_stats.faults_corrected = 0;
    aegis_stats.quarantined_pages = 0;

    for (int i = 0; i < AEGIS_MAX_PAGES; i++) {
        aegis_shadow[i] = 0;
    }

    quarantine_count = 0;

    write_serial("[AEGIS] Aegis protection active on ");
    write_serial_hex(AEGIS_MAX_PAGES);
    write_serial(" pages.\r\n");
}

/* Protect a page: compute and store its CRC32 */
void aegis_protect_page(void* phys_addr) {
    uint32_t addr = (uint32_t)phys_addr;
    uint32_t page = addr / 4096;

    if (page >= AEGIS_MAX_PAGES) return;
    if (aegis_is_quarantined(phys_addr)) return;

    uint32_t crc = aegis_crc32((const uint8_t*)addr, 4096);
    aegis_shadow[page] = crc;
    aegis_stats.pages_protected++;
}

/* Verify a page's CRC checksum.
 * Returns: 0 = OK, 2 = uncorrectable (quarantined)
 *
 * Note: CRC32 detects corruption but cannot correct it.
 * Single-bit brute-force correction was removed because
 * CRC32 is not a linear error-correcting code -
 * two different data patterns can produce the same CRC.
 * True ECC requires hardware support or Hamming codes. */
int aegis_verify_page(void* phys_addr) {
    uint32_t addr = (uint32_t)phys_addr;
    uint32_t page = addr / 4096;

    if (page >= AEGIS_MAX_PAGES) return 0;
    if (aegis_shadow[page] == 0) return 0;

    uint32_t expected = aegis_shadow[page];
    uint32_t actual = aegis_crc32((const uint8_t*)addr, 4096);

    if (expected == actual) return 0;

    /* Checksum mismatch - fault detected */
    aegis_stats.faults_detected++;

    write_serial("[AEGIS] FAULT: Page 0x");
    write_serial_hex(addr);
    write_serial(" CRC mismatch (detected, cannot correct)\r\n");

    /* Quarantine this page - it has unrecoverable corruption */
    if (quarantine_count < MAX_QUARANTINED) {
        quarantine_list[quarantine_count++] = addr;
        aegis_stats.quarantined_pages = quarantine_count;
    }

    return 2;
}

/* Remove protection when a page is freed */
void aegis_unprotect_page(void* phys_addr) {
    uint32_t addr = (uint32_t)phys_addr;
    uint32_t page = addr / 4096;
    if (page < AEGIS_MAX_PAGES) {
        aegis_shadow[page] = 0;
        if (aegis_stats.pages_protected > 0)
            aegis_stats.pages_protected--;
    }
}

/* Scan all protected pages for bit rot. Returns number of faults found. */
int aegis_scrub_scan(void) {
    int faults = 0;
    for (uint32_t i = 0; i < AEGIS_MAX_PAGES; i++) {
        if (aegis_shadow[i] != 0) {
            void* addr = (void*)(i * 4096);
            if (!aegis_is_quarantined(addr)) {
                int result = aegis_verify_page(addr);
                if (result > 0) faults++;
            }
        }
    }
    return faults;
}

/* Check if a physical address is quarantined */
int aegis_is_quarantined(void* phys_addr) {
    uint32_t addr = (uint32_t)phys_addr;
    for (int i = 0; i < quarantine_count; i++) {
        uint32_t q_page = quarantine_list[i] & ~0xFFF;
        uint32_t a_page = addr & ~0xFFF;
        if (q_page == a_page) return 1;
    }
    return 0;
}

/* Get Aegis stats */
aegis_stats_t* get_aegis_stats(void) {
    return &aegis_stats;
}

/* Check quarantine by physical address value (for PMM visualizer use) */
uint32_t aegis_quarantine_check(uint32_t phys_addr) {
    uint32_t a_page = phys_addr & ~0xFFF;
    for (int i = 0; i < quarantine_count; i++) {
        uint32_t q_page = quarantine_list[i] & ~0xFFF;
        if (q_page == a_page) return 1;
    }
    return 0;
}

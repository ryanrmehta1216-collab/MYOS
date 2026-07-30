#include "ata.h"
#include "serial.h"
#include "memory.h"

/* -----------------------------------------------
 * ATA PIO Mode Driver
 *
 * Uses PIO (Programmed I/O) to communicate with
 * the ATA controller. This is the simplest way to
 * interact with IDE/ATA hard drives.
 *
 * Supports LBA28 addressing (up to 128 GB).
 * ----------------------------------------------- */

static int ata_present = 0;

/* Read a byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write a byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Wait 400ns for drive to be ready */
static void ata_wait(void) {
    inb(ATA_PRIMARY_ALTSTAT);
    inb(ATA_PRIMARY_ALTSTAT);
    inb(ATA_PRIMARY_ALTSTAT);
    inb(ATA_PRIMARY_ALTSTAT);
}

/* Poll the status register until BSY clears and DRQ is set */
static int ata_poll(void) {
    ata_wait();
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_SR_BSY)) {
            if (status & ATA_SR_ERR) return -1;
            if (status & ATA_SR_DRQ) return 0;
        }
    }
    return -1; /* Timeout */
}

/* Identify the ATA drive */
static int ata_identify(void) {
    /* Select master drive on primary bus */
    outb(ATA_PRIMARY_DRIVE, 0xA0);

    /* Send IDENTIFY command */
    outb(ATA_PRIMARY_SECTORS, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MI, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    outb(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    /* Check if drive exists */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        write_serial("[ATA] No drive on primary master.\r\n");
        return -1;
    }

    /* Poll */
    status = inb(ATA_PRIMARY_STATUS);
    int timeout = 0;
    while ((status & ATA_SR_BSY) && timeout < 100000) {
        status = inb(ATA_PRIMARY_STATUS);
        timeout++;
    }

    if (status & ATA_SR_ERR) {
        /* ATAPI device? */
        uint8_t err = inb(ATA_PRIMARY_ERR);
        if (err & 0x01) {
            write_serial("[ATA] ATAPI device (not ATA).\r\n");
        }
        return -1;
    }

    ata_wait();

    /* Read identify data (256 words) — discard, we just confirm the drive exists */
    for (int i = 0; i < 256; i++) {
        (void)inb(ATA_PRIMARY_DATA);  /* Read low byte */
        (void)inb(ATA_PRIMARY_DATA);  /* Read high byte */
    }

    write_serial("[ATA] Drive identified.\r\n");
    return 0;
}

/* Initialize ATA driver */
void init_ata(void) {
    write_serial("[ATA] Initializing ATA PIO driver...\r\n");

    if (ata_identify() == 0) {
        ata_present = 1;
        write_serial("[ATA] ATA drive ready on primary master.\r\n");
    } else {
        write_serial("[ATA] No ATA drive found.\r\n");
    }
}

/* Read a single 512-byte sector via PIO */
int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!ata_present) return -1;

    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECTORS, 1);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MI, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_CMD, ATA_CMD_READ_PIO);

    if (ata_poll() != 0) {
        write_serial("[ATA] Read error at LBA ");
        write_serial_hex(lba);
        write_serial("\r\n");
        return -1;
    }

    /* Read 256 words (512 bytes) */
    for (int i = 0; i < 256; i++) {
        uint16_t word = inb(ATA_PRIMARY_DATA) | (inb(ATA_PRIMARY_DATA) << 8);
        buffer[i * 2] = word & 0xFF;
        buffer[i * 2 + 1] = (word >> 8) & 0xFF;
    }

    return 0;
}

/* Write a single 512-byte sector via PIO */
int ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    if (!ata_present) return -1;

    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECTORS, 1);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MI, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_CMD, ATA_CMD_WRITE_PIO);

    if (ata_poll() != 0) {
        write_serial("[ATA] Write error at LBA ");
        write_serial_hex(lba);
        write_serial("\r\n");
        return -1;
    }

    /* Write 256 words (512 bytes) */
    for (int i = 0; i < 256; i++) {
        uint16_t word = buffer[i * 2] | (buffer[i * 2 + 1] << 8);
        outb(ATA_PRIMARY_DATA, word & 0xFF);
        outb(ATA_PRIMARY_DATA, (word >> 8) & 0xFF);
    }

    /* Flush write cache */
    outb(ATA_PRIMARY_CMD, ATA_CMD_FLUSH);
    ata_poll();

    return 0;
}

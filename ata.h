#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* -----------------------------------------------
 * ATA PIO Mode Driver (Phase 6.1)
 *
 * Primary IDE controller at I/O ports 0x1F0-0x1F7
 * Secondary IDE controller at I/O ports 0x170-0x177
 *
 * Uses Programmed I/O (PIO) mode to read/write
 * individual 512-byte sectors.
 * ----------------------------------------------- */

/* ATA primary bus I/O ports */
#define ATA_PRIMARY_DATA     0x1F0
#define ATA_PRIMARY_ERR      0x1F1
#define ATA_PRIMARY_SECTORS  0x1F2
#define ATA_PRIMARY_LBA_LO   0x1F3
#define ATA_PRIMARY_LBA_MI   0x1F4
#define ATA_PRIMARY_LBA_HI   0x1F5
#define ATA_PRIMARY_DRIVE    0x1F6
#define ATA_PRIMARY_CMD      0x1F7
#define ATA_PRIMARY_STATUS   0x1F7
#define ATA_PRIMARY_ALTSTAT  0x3F6

/* ATA commands */
#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_FLUSH        0xE7

/* ATA status register bits */
#define ATA_SR_ERR           0x01
#define ATA_SR_DRQ           0x08
#define ATA_SR_DF            0x20
#define ATA_SR_DRDY          0x40
#define ATA_SR_BSY           0x80

/* Sector size */
#define ATA_SECTOR_SIZE      512

/* Initialize ATA driver */
void init_ata(void);

/* Read one 512-byte sector */
int ata_read_sector(uint32_t lba, uint8_t* buffer);

/* Write one 512-byte sector */
int ata_write_sector(uint32_t lba, const uint8_t* buffer);

#endif /* ATA_H */

#ifndef MEHTAFS_H
#define MEHTAFS_H

#include <stdint.h>

/* -----------------------------------------------
 * MehtaFS — Log-Structured Custom File System
 *
 * A minimal, append-only log-structured filesystem
 * designed for embedded/OSdev use.
 *
 * Disk layout:
 *   Sector 0: Superblock
 *   Sector 1+: Log entries (inodes + data blocks)
 *
 * Each log entry is a self-describing segment:
 *   [CRC32] [type] [name] [data...]
 *
 * On mount, the log is replayed to build the VFS tree.
 *
 * Architecture:
 *   Log-structured = all writes are sequential appends.
 *   This eliminates seeking and provides crash recovery.
 *   A cleaner thread can later compact the log.
 * ----------------------------------------------- */

/* Superblock magic */
#define MEHTAFS_MAGIC  0x4D485453  /* "MHTS" */

/* Maximum files */
#define MEHTAFS_MAX_FILES 64

/* File types */
#define MEHTAFS_FILE      0x01
#define MEHTAFS_DIR       0x02

/* Log entry types */
#define MEHTAFS_ENTRY_FILE   1
#define MEHTAFS_ENTRY_DELETE 2

/* Superblock structure (sector 0) */
typedef struct {
    uint32_t magic;          /* MEHTAFS_MAGIC */
    uint32_t version;        /* Version number */
    uint32_t total_sectors;  /* Total sectors on device */
    uint32_t log_start;      /* First log sector */
    uint32_t log_end;        /* Next free log sector (append point) */
    uint32_t file_count;     /* Number of files tracked */
    uint32_t crc32;          /* Superblock checksum */
} __attribute__((packed)) mehtafs_super_t;

/* Log entry header (precedes every update to the log) */
typedef struct {
    uint32_t crc32;          /* CRC of entire entry (including name + data) */
    uint8_t  entry_type;     /* MEHTAFS_ENTRY_FILE or MEHTAFS_ENTRY_DELETE */
    uint8_t  file_type;      /* MEHTAFS_FILE or MEHTAFS_DIR */
    uint8_t  name_len;       /* Length of filename */
    uint8_t  reserved;
    uint32_t data_len;       /* Length of file data */
    uint32_t timestamp;      /* Creation/modification time (ticks) */
} __attribute__((packed)) mehtafs_entry_t;

/* Initialize MehtaFS */
void init_mehtafs(void);

/* Format a disk with MehtaFS */
int mehtafs_format(void);

/* Mount MehtaFS (replay log into VFS) */
int mehtafs_mount(void);

/* Write a file to MehtaFS (append to log) */
int mehtafs_write_file(const char* name, const uint8_t* data, uint32_t length);

/* Read a file from MehtaFS (into kmalloc'd buffer) */
uint8_t* mehtafs_read_file(const char* name, uint32_t* out_length);

#endif /* MEHTAFS_H */

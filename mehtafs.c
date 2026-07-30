#include "mehtafs.h"
#include "ata.h"
#include "vfs.h"
#include "serial.h"
#include "memory.h"

/* Heap allocator declarations */
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

/* -----------------------------------------------
 * MehtaFS Implementation
 *
 * Log-structured filesystem stored on ATA disk.
 * All writes are appended sequentially to the log.
 * On mount, the log is replayed to reconstruct files.
 *
 * This design provides:
 *   1. Crash recovery (log is always consistent)
 *   2. Minimal seeking (sequential writes)
 *   3. Simple garbage collection (discard old entries)
 * ----------------------------------------------- */

/* Current superblock (cached in memory) */
static mehtafs_super_t super;
static int mehtafs_mounted = 0;

/* Recompute CRC32 (same polynomial as Aegis) */
extern uint32_t aegis_crc32(const uint8_t* data, size_t length);

/* Initialize MehtaFS */
void init_mehtafs(void) {
    write_serial("[FS] Initializing MehtaFS...\r\n");

    /* Try to mount existing filesystem */
    if (mehtafs_mount() == 0) {
        write_serial("[FS] MehtaFS mounted successfully.\r\n");
    } else {
        write_serial("[FS] No existing MehtaFS found. Formatting...\r\n");
        if (mehtafs_format() == 0) {
            write_serial("[FS] MehtaFS formatted and mounted.\r\n");
        } else {
            write_serial("[FS] MehtaFS initialization failed (no disk).\r\n");
        }
    }
}

/* Format disk with MehtaFS */
int mehtafs_format(void) {
    super.magic = MEHTAFS_MAGIC;
    super.version = 1;
    super.total_sectors = 65536; /* ~32 MB disk */
    super.log_start = 1;          /* Superblock at sector 0, log starts at sector 1 */
    super.log_end = 1;            /* Append point starts at sector 1 */
    super.file_count = 0;

    /* Compute superblock CRC */
    super.crc32 = aegis_crc32((const uint8_t*)&super + 4, sizeof(super) - 4);

    /* Write superblock to disk */
    if (ata_write_sector(0, (const uint8_t*)&super) != 0) {
        return -1; /* No disk */
    }

    mehtafs_mounted = 1;
    return 0;
}

/* Mount: read superblock and replay log */
int mehtafs_mount(void) {
    /* Try to read superblock from disk */
    if (ata_read_sector(0, (uint8_t*)&super) != 0) {
        return -1; /* No disk */
    }

    if (super.magic != MEHTAFS_MAGIC) {
        return -2; /* Not a MehtaFS disk */
    }

    mehtafs_mounted = 1;

    /* Replay log: read log entries from log_start to log_end */
    uint32_t current_sector = super.log_start;
    uint8_t* sector_buf = (uint8_t*)kmalloc(ATA_SECTOR_SIZE);
    if (!sector_buf) return -3;

    while (current_sector < super.log_end) {
        if (ata_read_sector(current_sector, sector_buf) != 0) {
            break;
        }

        mehtafs_entry_t* entry = (mehtafs_entry_t*)sector_buf;

        if (entry->entry_type == MEHTAFS_ENTRY_FILE) {
            /* Extract name */
            char* name = (char*)(sector_buf + sizeof(mehtafs_entry_t));
            uint8_t* data = (uint8_t*)(name + entry->name_len);

            /* Null-terminate name */
            char name_copy[64];
            int i;
            for (i = 0; i < entry->name_len && i < 63; i++) {
                name_copy[i] = name[i];
            }
            name_copy[i] = '\0';

            /* Add to VFS */
            extern void vfs_add_file(const char* name, const char* content, uint32_t length);
            vfs_add_file(name_copy, (const char*)data, entry->data_len);
            super.file_count++;
        }
        /* MEHTAFS_ENTRY_DELETE: skip for now */

        current_sector++;
    }

    kfree(sector_buf);

    write_serial("[FS] Replayed ");
    write_serial_hex(super.file_count);
    write_serial(" files from log.\r\n");

    return 0;
}

/* Write a file to MehtaFS (append to log) */
int mehtafs_write_file(const char* name, const uint8_t* data, uint32_t length) {
    if (!mehtafs_mounted) return -1;

    uint32_t name_len = 0;
    while (name[name_len] != '\0' && name_len < 255) name_len++;

    if (name_len == 0) return -2;

    /* Calculate total entry size */
    uint32_t entry_size = sizeof(mehtafs_entry_t) + name_len + length;

    /* We need ceil(entry_size / 512) sectors */
    uint32_t sectors_needed = (entry_size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

    /* Allocate buffer for the entry */
    uint32_t buf_size = sectors_needed * ATA_SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)kmalloc(buf_size);
    if (!buf) return -3;

    /* Clear buffer */
    for (uint32_t i = 0; i < buf_size; i++) buf[i] = 0;

    /* Build entry header */
    mehtafs_entry_t* entry = (mehtafs_entry_t*)buf;
    entry->entry_type = MEHTAFS_ENTRY_FILE;
    entry->file_type = MEHTAFS_FILE;
    entry->name_len = name_len;
    entry->data_len = length;
    entry->timestamp = 0; /* TODO: use timer_ticks */
    entry->reserved = 0;

    /* Copy name and data */
    uint8_t* ptr = buf + sizeof(mehtafs_entry_t);
    for (uint32_t i = 0; i < name_len; i++) ptr[i] = (uint8_t)name[i];

    ptr = buf + sizeof(mehtafs_entry_t) + name_len;
    for (uint32_t i = 0; i < length; i++) ptr[i] = data[i];

    /* Compute CRC */
    entry->crc32 = aegis_crc32(buf + 4, entry_size - 4);

    /* Write to disk at current log end */
    for (uint32_t i = 0; i < sectors_needed; i++) {
        if (ata_write_sector(super.log_end + i, buf + i * ATA_SECTOR_SIZE) != 0) {
            kfree(buf);
            return -4;
        }
    }

    super.log_end += sectors_needed;
    super.file_count++;

    /* Update superblock on disk */
    super.crc32 = aegis_crc32((const uint8_t*)&super + 4, sizeof(super) - 4);
    ata_write_sector(0, (const uint8_t*)&super);

    kfree(buf);

    write_serial("[FS] Wrote \"");
    write_serial(name);
    write_serial("\" to MehtaFS.\r\n");

    return 0;
}

/* Read a file from MehtaFS by name */
uint8_t* mehtafs_read_file(const char* name, uint32_t* out_length) {
    if (!mehtafs_mounted) return NULL;

    /* Walk the log from end to find most recent version */
    uint32_t current = super.log_end;
    uint8_t* sector_buf = (uint8_t*)kmalloc(ATA_SECTOR_SIZE);

    while (current > super.log_start) {
        current--;
        if (ata_read_sector(current, sector_buf) != 0) break;

        mehtafs_entry_t* entry = (mehtafs_entry_t*)sector_buf;

        if (entry->entry_type == MEHTAFS_ENTRY_FILE) {
            char* entry_name = (char*)(sector_buf + sizeof(mehtafs_entry_t));

            /* Compare names */
            int match = 1;
            uint32_t i;
            for (i = 0; i < entry->name_len; i++) {
                if (name[i] != entry_name[i]) { match = 0; break; }
            }
            if (match && name[entry->name_len] == '\0') {
                /* Found it! */
                uint8_t* data = (uint8_t*)kmalloc(entry->data_len + 1);
                uint8_t* src = sector_buf + sizeof(mehtafs_entry_t) + entry->name_len;
                for (uint32_t j = 0; j < entry->data_len; j++) {
                    data[j] = src[j];
                }
                data[entry->data_len] = '\0';
                *out_length = entry->data_len;
                kfree(sector_buf);
                return data;
            }
        }
    }

    kfree(sector_buf);
    return NULL;
}

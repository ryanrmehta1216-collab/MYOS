#ifndef CAPABILITY_H
#define CAPABILITY_H

#include <stdint.h>
#include <stdbool.h>

// Maximum security tokens a single process can hold
#define MAX_CAPABILITIES_PER_PROC 32

// Hardware & Resource boundaries
typedef enum {
    CAP_TYPE_NONE        = 0,
    CAP_TYPE_FRAMEBUFFER = 1, // Access to draw pixels
    CAP_TYPE_IO_PORT     = 2, // Access to inb/outb ports
    CAP_TYPE_MEMORY_MAP  = 3, // Access to specific RAM pages
    CAP_TYPE_IPC_SEND    = 4  // Inter-process communication
} cap_type_t;

// The capability token structure
typedef struct {
    uint32_t cap_id;       // Unique token ID (e.g. 0x100)
    cap_type_t type;       // What resource this protects
    uint32_t target_addr;  // Base address or port ID
    uint32_t length;       // Allowed memory window range
    uint32_t permissions;  // Bitwise flags: 0x01 = Read, 0x02 = Write
} capability_t;

// The capability table attached to a process
typedef struct {
    capability_t caps[MAX_CAPABILITIES_PER_PROC];
    uint32_t total_caps;
} cap_table_t;

// Function prototypes
bool zt_verify_capability(cap_table_t* proc_caps, uint32_t cap_id, cap_type_t required_type, uint32_t required_perm);
void init_default_process_capabilities(cap_table_t* table);

#endif
#include "capability.h"

// The core Zero-Trust Verification Engine (Default Deny)
bool zt_verify_capability(cap_table_t* proc_caps, uint32_t cap_id, cap_type_t required_type, uint32_t required_perm) {
    if (!proc_caps) return false;

    for (uint32_t i = 0; i < MAX_CAPABILITIES_PER_PROC; i++) {
        capability_t* cap = &proc_caps->caps[i];
        
        // Match token ID and resource type
        if (cap->cap_id == cap_id && cap->type == required_type) {
            // Verify bitmask permissions (e.g., asking for Write, must have Write flag 0x02)
            if ((cap->permissions & required_perm) == required_perm) {
                return true; // AUTHORIZED
            }
        }
    }

    return false; // DENIED - Default Deny Security Boundary Enforced
}

// Gives a newly spawned process its initial restricted tokens
void init_default_process_capabilities(cap_table_t* table) {
    if (!table) return;

    // Zero out all capability slots
    for (int i = 0; i < MAX_CAPABILITIES_PER_PROC; i++) {
        table->caps[i].cap_id = 0;
        table->caps[i].type = CAP_TYPE_NONE;
        table->caps[i].target_addr = 0;
        table->caps[i].length = 0;
        table->caps[i].permissions = 0;
    }
    
    // Grant exactly ONE token: Token 0x100 for Framebuffer Write Access
    table->caps[0].cap_id = 0x100;
    table->caps[0].type = CAP_TYPE_FRAMEBUFFER;
    table->caps[0].target_addr = 0xFD000000;
    table->caps[0].length = 1024 * 768 * 4;
    table->caps[0].permissions = 0x02; // 0x02 = Write-only permission
    
    table->total_caps = 1;
}
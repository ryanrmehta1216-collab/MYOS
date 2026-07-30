#include <stdint.h>
#include "serial.h"

/* -----------------------------------------------
 * Kernel Main — Entry point from boot.s
 * 
 * Complete initialization sequence for the MYOS kernel
 * with multitasking, security, filesystem, networking,
 * and live telemetry.
 *
 * Phases:
 *   0. Serial port (early debug output)
 *   1. GDT & TSS (segment setup)
 *   2. IDT + PIC remap (interrupts)
 *   3. PMM (physical memory manager)
 *   4. Paging (virtual memory)
 *   5. Heap (kmalloc/kfree)
 *   6. Timer + Scheduler (PIT at 100 Hz)
 *   7. Keyboard (PS/2 IRQ1)
 *   8. Mouse (PS/2 IRQ12)
 *   9. Aegis Memory (ECC + fault quarantine)
 *   10. Desktop compositor + windows
 *   11. User mode subsystem + syscalls
 *   12. ATA disk driver
 *   13. MehtaFS filesystem
 *   14. PCI bus enumeration
 *   15. RTL8139 network driver
 *   16. Aura-Net networking stack
 *   17. Enable interrupts
 *   18. Enter desktop / telemetry loop
 * ----------------------------------------------- */

/* Forward declarations of all initialization functions */
void init_gdt(void);
void init_tss(uint32_t kernel_esp);
void init_idt(void);
void pmm_init(uint32_t mem_lower, uint32_t mem_upper);
void init_paging(void);
void init_heap(void);
void slab_init(void);
void init_timer(void);
void init_keyboard(void);
void init_mouse(void);
void init_syscalls(void);
void init_user_subsystem(void);
void init_scheduler(void);
void init_desktop(void);
void desktop_task(void);
void idle_task(void);
void init_ata(void);
void init_mehtafs(void);
void init_pci(void);
void init_rtl8139(void);
void init_aura_net(void);
void aegis_init(void);
void init_telemetry(void);

extern uint32_t end;
uint32_t kernel_start = 0x100000;
uint32_t kernel_end;

/* GRUB Multiboot information structure */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  vbe_reserved[3];
} __attribute__((packed)) multiboot_info_t;

void kernel_main(uint32_t magic, multiboot_info_t* mbi) {
    /* ---- Phase 0: Serial port ---- */
    init_serial();

    write_serial("\r\n");
    write_serial("================================================================\r\n");
    write_serial("        MYOS v2.0 — 32-bit x86 Advanced Operating System        \r\n");
    write_serial("================================================================\r\n");

    if (magic != 0x2BADB002) {
        write_serial("[KERNEL] WARNING: Not booted by a Multiboot-compliant bootloader!\r\n");
    }

    uint32_t mem_lower = mbi->mem_lower;
    uint32_t mem_upper = mbi->mem_upper;
    kernel_end = (uint32_t)&end;

    write_serial("[KERNEL] Memory: lower=");
    write_serial_hex(mem_lower);
    write_serial("KB upper=");
    write_serial_hex(mem_upper);
    write_serial("KB kernel=0x");
    write_serial_hex(kernel_end);
    write_serial("\r\n");

    /* ---- Phase 1: GDT + TSS ---- */
    write_serial("[KERNEL] Phase 1: GDT...\r\n");
    init_gdt();

    /* ---- Phase 2: TSS ---- */
    uint32_t kernel_stack = 0x100000 + 0x4000;
    init_tss(kernel_stack);

    /* ---- Phase 3: IDT + PIC ---- */
    write_serial("[KERNEL] Phase 2: IDT & PIC...\r\n");
    init_idt();

    /* ---- Phase 4: PMM ---- */
    write_serial("[KERNEL] Phase 3: PMM...\r\n");
    pmm_init(mem_lower, mem_upper);

    /* ---- Phase 5: Paging ---- */
    write_serial("[KERNEL] Phase 4: Paging...\r\n");
    init_paging();

    /* ---- Phase 6: Heap ---- */
    write_serial("[KERNEL] Phase 5: Heap...\r\n");
    init_heap();

    /* ---- Phase 7: Slab Allocator ---- */
    write_serial("[KERNEL] Phase 6: Slab Allocator...\r\n");
    slab_init();

    /* ---- Phase 8: Timer + Scheduler ---- */
    write_serial("[KERNEL] Phase 7: Timer + Scheduler...\r\n");
    init_scheduler();
    init_timer();

    /* ---- Phase 9: Keyboard ---- */
    write_serial("[KERNEL] Phase 7: Keyboard...\r\n");
    init_keyboard();

    /* ---- Phase 9: Mouse ---- */
    write_serial("[KERNEL] Phase 8: Mouse...\r\n");
    init_mouse();

    /* ---- Phase 11: Aegis Memory Protection ---- */
    write_serial("[KERNEL] Phase 10: Aegis Memory...\r\n");
    aegis_init();

    /* ---- Phase 12: Desktop ---- */
    write_serial("[KERNEL] Phase 10: Desktop...\r\n");
    init_desktop();

    /* ---- Phase 13: Syscalls + User Mode ---- */
    write_serial("[KERNEL] Phase 12: Syscalls & User Mode...\r\n");
    init_syscalls();
    init_user_subsystem();

    /* ---- Phase 14: ATA Disk ---- */
    write_serial("[KERNEL] Phase 12: ATA...\r\n");
    init_ata();

    /* ---- Phase 15: MehtaFS ---- */
    write_serial("[KERNEL] Phase 14: MehtaFS...\r\n");
    init_mehtafs();

    /* ---- Phase 16: PCI ---- */
    write_serial("[KERNEL] Phase 14: PCI...\r\n");
    init_pci();

    /* ---- Phase 17: RTL8139 ---- */
    write_serial("[KERNEL] Phase 16: RTL8139...\r\n");
    init_rtl8139();

    /* ---- Phase 18: Aura-Net ---- */
    write_serial("[KERNEL] Phase 16: Aura-Net...\r\n");
    init_aura_net();

    /* ---- Phase 19: Telemetry ---- */
    write_serial("[KERNEL] Phase 18: Telemetry...\r\n");
    init_telemetry();

    /* ---- Clear VBE framebuffer (remove boot garbage) ---- */
    extern void clear_screen_gfx(uint32_t color);
    extern void gfx_flip(void);
    clear_screen_gfx(0x1E1E2E);  /* Clear to Catppuccin background */
    gfx_flip();                   /* Push clean frame to screen NOW */

    /* ---- Enable interrupts ---- */
    write_serial("[KERNEL] All subsystems initialized. Enabling interrupts...\r\n");
    __asm__ volatile("sti");

    /* ---- Enter desktop GUI loop ---- */
    write_serial("[KERNEL] Entering desktop compositor loop.\r\n");
    write_serial("================================================================\r\n\r\n");

    desktop_task();

    /* Should never reach here */
    write_serial("[KERNEL] ERROR: desktop_task returned!\r\n");
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

/* Stub implementations for subsystems not yet written.
 * These are weak aliases so the linker uses them when the
 * real implementation files are not yet compiled.
 */

void __attribute__((weak)) aegis_init(void) {
    write_serial("[AEGIS] Aegis memory protection: not available (stub)\r\n");
}

void __attribute__((weak)) init_ata(void) {
    write_serial("[ATA] ATA driver: not available (stub)\r\n");
}

void __attribute__((weak)) init_mehtafs(void) {
    write_serial("[FS] MehtaFS: not available (stub)\r\n");
}

void __attribute__((weak)) init_pci(void) {
    write_serial("[PCI] PCI enumeration: not available (stub)\r\n");
}

void __attribute__((weak)) init_rtl8139(void) {
    write_serial("[NET] RTL8139: not available (stub)\r\n");
}

void __attribute__((weak)) init_aura_net(void) {
    write_serial("[NET] Aura-Net: not available (stub)\r\n");
}

void __attribute__((weak)) init_telemetry(void) {
    write_serial("[TELEM] Telemetry: not available (stub)\r\n");
}

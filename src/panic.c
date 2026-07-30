#include "panic.h"
#include "serial.h"

/* -----------------------------------------------
 * Kernel Panic — Blue Screen of Death
 *
 * Called when a CPU exception or fatal kernel
 * error occurs. Halts the CPU and draws a
 * detailed register dump to the framebuffer.
 *
 * The BSOD shows:
 *   - Error message
 *   - All general-purpose registers (EAX, EBX, etc.)
 *   - Segment registers (CS, DS, ES, FS, GS)
 *   - Control registers (CR0, CR2, CR3)
 *   - Stack trace (EIP, ESP, EBP)
 *   - EFLAGS
 * ----------------------------------------------- */

/* Forward declare graphics functions we use directly */
extern void clear_screen_gfx(uint32_t color);
extern void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color);
extern void draw_string_bg(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);
extern void set_draw_bg(uint32_t bg_color);
extern void gfx_flip(void);

/* Hex digit helper */
static char hex_digit(uint32_t val) {
    val &= 0xF;
    return (val < 10) ? ('0' + val) : ('A' + val - 10);
}

/* Format a 32-bit hex value into a buffer */
static void format_hex(char* buf, uint32_t val) {
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex_digit(val >> (i * 4));
    }
    buf[10] = '\0';
}

/* Draw the BSOD */
void panic_draw_bsod(const char* message, registers_t* regs) {
    uint32_t bg       = 0x000080;  /* Classic blue */
    uint32_t fg       = 0xFFFFFF;  /* White text */
    uint32_t hdr_bg   = 0x000060;
    uint32_t accent   = 0xFFFF00;
    uint32_t reg_col  = 0xA0D0FF;  /* Light blue for register values */
    uint32_t sec_col  = 0xC0C0C0;  /* Light grey */

    int SCREEN_W = 1024;
    int SCREEN_H = 768;

    clear_screen_gfx(bg);
    set_draw_bg(bg);

    /* Title bar */
    draw_rect(0, 0, SCREEN_W, 40, hdr_bg);
    set_draw_bg(hdr_bg);
    draw_string(20, 12, "  *** KERNEL PANIC ***  MYOS v2.0  -  Blue Screen of Death", 0xFF6666);

    /* Error message */
    set_draw_bg(bg);
    draw_string(20, 60, "Error: ", accent);
    if (message) {
        draw_string(100, 60, message, fg);
    } else {
        draw_string(100, 60, "Unknown fatal error", fg);
    }

    /* ---- CPU Registers (2-column layout) ---- */
    draw_string(20, 90, "===== CPU GENERAL-PURPOSE REGISTERS =====", accent);

    char buf[48];

    if (regs) {
        /* Left column: EAX, EBX, ECX, EDX */
        format_hex(buf, regs->eax); draw_string(20, 110, "EAX: ", sec_col); draw_string(90, 110, buf, reg_col);
        format_hex(buf, regs->ebx); draw_string(20, 128, "EBX: ", sec_col); draw_string(90, 128, buf, reg_col);
        format_hex(buf, regs->ecx); draw_string(20, 146, "ECX: ", sec_col); draw_string(90, 146, buf, reg_col);
        format_hex(buf, regs->edx); draw_string(20, 164, "EDX: ", sec_col); draw_string(90, 164, buf, reg_col);

        /* Right column: ESI, EDI, EBP, ESP */
        format_hex(buf, regs->esi); draw_string(300, 110, "ESI: ", sec_col); draw_string(370, 110, buf, reg_col);
        format_hex(buf, regs->edi); draw_string(300, 128, "EDI: ", sec_col); draw_string(370, 128, buf, reg_col);
        format_hex(buf, regs->ebp); draw_string(300, 146, "EBP: ", sec_col); draw_string(370, 146, buf, reg_col);
        format_hex(buf, regs->esp); draw_string(300, 164, "ESP: ", sec_col); draw_string(370, 164, buf, reg_col);

        /* ---- Instruction pointer & flags ---- */
        draw_string(20, 194, "===== EXECUTION CONTEXT =====", accent);
        format_hex(buf, regs->eip);      draw_string(20, 214, "EIP:          ", sec_col); draw_string(160, 214, buf, reg_col);
        format_hex(buf, regs->eflags);   draw_string(20, 232, "EFLAGS:       ", sec_col); draw_string(160, 232, buf, reg_col);
        format_hex(buf, regs->cs);       draw_string(20, 250, "CS:           ", sec_col); draw_string(160, 250, buf, reg_col);
        format_hex(buf, regs->user_ss);  draw_string(20, 268, "SS (user):    ", sec_col); draw_string(160, 268, buf, reg_col);
        format_hex(buf, regs->user_esp); draw_string(20, 286, "ESP (user):   ", sec_col); draw_string(160, 286, buf, reg_col);

        /* ---- Segment registers ---- */
        draw_string(20, 316, "===== SEGMENT REGISTERS =====", accent);
        format_hex(buf, regs->ds);  draw_string(20, 336, "DS: ", sec_col); draw_string(90, 336, buf, reg_col);
        format_hex(buf, regs->es);  draw_string(20, 354, "ES: ", sec_col); draw_string(90, 354, buf, reg_col);
        format_hex(buf, regs->fs);  draw_string(20, 372, "FS: ", sec_col); draw_string(90, 372, buf, reg_col);
        format_hex(buf, regs->gs);  draw_string(20, 390, "GS: ", sec_col); draw_string(90, 390, buf, reg_col);

        /* ---- Exception info ---- */
        draw_string(20, 420, "===== EXCEPTION INFO =====", accent);
        format_hex(buf, regs->int_no);   draw_string(20, 440, "Exception #:  ", sec_col); draw_string(160, 440, buf, reg_col);
        format_hex(buf, regs->err_code); draw_string(20, 458, "Error Code:   ", sec_col); draw_string(160, 458, buf, reg_col);

        /* Exception name lookup */
        const char* exc_name = "Unknown";
        switch (regs->int_no) {
            case 0:  exc_name = "Division Error";          break;
            case 1:  exc_name = "Debug Exception";         break;
            case 2:  exc_name = "Non-Maskable Interrupt";  break;
            case 3:  exc_name = "Breakpoint";              break;
            case 4:  exc_name = "Overflow";                break;
            case 5:  exc_name = "Bound Range Exceeded";    break;
            case 6:  exc_name = "Invalid Opcode";          break;
            case 7:  exc_name = "Device Not Available";    break;
            case 8:  exc_name = "Double Fault";            break;
            case 10: exc_name = "Invalid TSS";             break;
            case 11: exc_name = "Segment Not Present";     break;
            case 12: exc_name = "Stack Segment Fault";     break;
            case 13: exc_name = "General Protection Fault";break;
            case 14: exc_name = "Page Fault";              break;
            case 16: exc_name = "x87 FPU Error";           break;
            case 17: exc_name = "Alignment Check";         break;
            case 18: exc_name = "Machine Check";           break;
            case 19: exc_name = "SIMD FPU Exception";      break;
        }
        draw_string(300, 440, "Name: ", sec_col); draw_string(370, 440, exc_name, 0xFF8888);
    } else {
        draw_string(20, 110, "No register context available.", sec_col);
    }

    /* ---- Control registers (only if regs provided) ---- */
    if (regs) {
        draw_string(20, 490, "===== CONTROL REGISTERS =====", accent);
        uint32_t cr0, cr2, cr3;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

        format_hex(buf, cr0); draw_string(20, 510, "CR0: ", sec_col); draw_string(90, 510, buf, reg_col);
        format_hex(buf, cr2); draw_string(300, 510, "CR2 (fault addr): ", sec_col); draw_string(450, 510, buf, reg_col);
        format_hex(buf, cr3); draw_string(20, 528, "CR3 (page dir): ", sec_col); draw_string(160, 528, buf, reg_col);
    }

    /* Divider */
    draw_rect(20, 558, SCREEN_W - 40, 1, 0xFFFFFF);

    /* Bottom hints */
    set_draw_bg(bg);
    draw_string(20, SCREEN_H - 60, "System halted. No recovery possible — reboot required.", sec_col);
    draw_string(20, SCREEN_H - 40, "Check serial debug output for detailed exception info.", sec_col);
}

/* The panic function — halts the system with a BSOD */
void panic(const char* message, registers_t* regs) {
    /* Disable interrupts */
    __asm__ volatile("cli");

    /* Log to serial */
    write_serial("\r\n========================================================\r\n");
    write_serial("  *** KERNEL PANIC ***\r\n");
    write_serial("========================================================\r\n");
    if (message) {
        write_serial("Message: ");
        write_serial(message);
        write_serial("\r\n");
    }

    /* Draw BSOD to framebuffer */
    panic_draw_bsod(message, regs);
    gfx_flip();

    /* Halt forever */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

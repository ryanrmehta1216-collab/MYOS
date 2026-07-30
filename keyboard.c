#include <stdint.h>
#include "interrupts.h"

/* -----------------------------------------------
 * PS/2 Keyboard Driver (IRQ1) — US QWERTY Set 1
 *
 * Decodes PS/2 Set 1 scancodes into ASCII with
 * full Shift, Caps Lock, Ctrl, and Alt support.
 *
 * PS/2 Set 1 scancode format:
 *   - Make code:    byte (0x00-0x7F)
 *   - Break code:   byte | 0x80
 *   - Extended:     0xE0 prefix before scancode
 *   - Pause/Break:  special multi-byte sequence
 *
 * The lookup tables map make-codes directly to
 * their ASCII values, with modifier keys tracked
 * in state variables for Shift/Caps resolution.
 * ----------------------------------------------- */

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Global text buffer — accessible from GUI compositor */
char     keyboard_buffer[256] = {0};
uint32_t keyboard_idx = 0;
uint8_t  keyboard_updated = 0;
uint8_t  shift_pressed   = 0;
uint8_t  ctrl_pressed    = 0;
uint8_t  alt_pressed     = 0;
uint8_t  caps_locked     = 0;

/* Flag set when F1 (scancode 0x3B) is pressed — consumed by desktop loop */
volatile uint8_t  f1_pressed  = 0;

/* 0xE0 extended prefix tracking:
 * When we see 0xE0, the NEXT byte is an extended scancode.
 * We flag it so the handler skips the next byte entirely. */
static uint8_t extended_prefix = 0;

/* ===========================================================
 * PS/2 Set 1 Scancode → ASCII Lookup Tables
 *
 * Index = make code byte (0x00-0x7F)
 * 0 = unprintable / modifier key (filtered separately)
 *
 * Table layout verified against standard IBM PC AT
 * keyboard Set 1 scancode assignments.
 * =========================================================== */

/* Unshifted (lowercase) */
static const char scancode_ascii_lower[128] = {
     0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',  0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',  0, '*',  0, ' ',
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};

/* Shifted (uppercase) */
static const char scancode_ascii_upper[128] = {
     0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',  0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',  0, '*',  0, ' ',
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};

/* Modifier key scancodes (Set 1) */
#define SCAN_LSHIFT_MAKE  0x2A
#define SCAN_RSHIFT_MAKE  0x36
#define SCAN_LSHIFT_BREAK 0xAA
#define SCAN_RSHIFT_BREAK 0xB6
#define SCAN_LCTRL_MAKE   0x1D
#define SCAN_LCTRL_BREAK  0x9D
#define SCAN_LALT_MAKE    0x38
#define SCAN_LALT_BREAK   0xB8
#define SCAN_CAPS_MAKE    0x3A
#define SCAN_CAPS_BREAK   0xBA
#define SCAN_F1_MAKE      0x3B
#define SCAN_EXTENDED     0xE0
#define SCAN_ENTER_MAKE   0x1C
#define SCAN_BACKSP_MAKE  0x0E

/* IRQ1 handler — called from irq_handler in idt.c */
static void keyboard_handler_c(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    /* ========== Extended prefix (0xE0) handling ==========
     * Some keys (arrow keys, Home, End, etc.) send 0xE0
     * before their actual scancode. When we see 0xE0,
     * we set a flag and return. On the next byte, the
     * flag causes us to skip it entirely so these keys
     * don't insert garbage characters. */
    if (scancode == SCAN_EXTENDED) {
        extended_prefix = 1;
        return;
    }

    if (extended_prefix) {
        extended_prefix = 0;
        return;  /* Discard the extended scancode byte */
    }

    /* ========== Modifier key tracking ==========
     * These keys don't produce characters but affect
     * the case of subsequent printable keys. We track
     * their pressed/released state through global vars. */

    /* Left/Right Shift */
    if (scancode == SCAN_LSHIFT_MAKE || scancode == SCAN_RSHIFT_MAKE)
        { shift_pressed = 1; return; }
    if (scancode == SCAN_LSHIFT_BREAK || scancode == SCAN_RSHIFT_BREAK)
        { shift_pressed = 0; return; }

    /* Left Ctrl */
    if (scancode == SCAN_LCTRL_MAKE)
        { ctrl_pressed = 1; return; }
    if (scancode == SCAN_LCTRL_BREAK)
        { ctrl_pressed = 0; return; }

    /* Left Alt */
    if (scancode == SCAN_LALT_MAKE)
        { alt_pressed = 1; return; }
    if (scancode == SCAN_LALT_BREAK)
        { alt_pressed = 0; return; }

    /* Caps Lock — toggle on make, ignore break.
     * CapsLock only affects A-Z, not other keys.
     * It toggles: if CapsLock is ON, letter output is
     * the opposite of Shift (Shift+CapsLock = lowercase). */
    if (scancode == SCAN_CAPS_MAKE) {
        caps_locked = !caps_locked;
        return;
    }
    if (scancode == SCAN_CAPS_BREAK) return;

    /* ========== Function keys ==========
     * F1–F10 use scancodes 0x3B–0x44 (make).
     * Set global flag for desktop loop to consume. */
    if (scancode == SCAN_F1_MAKE) {
        f1_pressed = 1;
        return;
    }

    /* ========== Filter key releases ==========
     * All make codes have bit 7 clear; break codes
     * have bit 7 set. We've already handled modifier
     * break codes above. For regular keys, break codes
     * are discarded. */
    if (scancode & 0x80) return;

    /* ========== Special character keys ========== */

    /* Backspace */
    if (scancode == SCAN_BACKSP_MAKE) {
        if (keyboard_idx > 0) {
            keyboard_idx--;
            keyboard_buffer[keyboard_idx] = '\0';
            keyboard_updated = 1;
        }
        return;
    }

    /* Enter */
    if (scancode == SCAN_ENTER_MAKE) {
        if (keyboard_idx < 254) {
            keyboard_buffer[keyboard_idx++] = '\n';
            keyboard_buffer[keyboard_idx] = '\0';
            keyboard_updated = 1;
        }
        return;
    }

    /* ========== Printable characters ==========
     * Use the scancode lookup table with proper
     * Shift/CapsLock interaction logic:
     *
     *   Output Case = Shift XOR CapsLock
     *
     * This means:
     *   - Shift OFF, Caps OFF → lowercase table
     *   - Shift ON,  Caps OFF → uppercase table
     *   - Shift OFF, Caps ON  → uppercase table (for letters)
     *   - Shift ON,  Caps ON  → lowercase table (shift reverses caps)
     *
     * IMPORTANT: CapsLock only applies to letters (A-Z, a-z).
     * Shift applies to ALL keys (numbers, symbols, letters).
     * So for non-letter keys, we always use Shift to decide. */
    if (scancode < 128) {
        char ch;
        uint8_t use_shift = shift_pressed;

        /* Apply CapsLock inversion only for letter keys (A-Z mapped to a-z) */
        unsigned char lower = (unsigned char)scancode_ascii_lower[scancode];
        if (caps_locked && lower >= 'a' && lower <= 'z') {
            use_shift = !use_shift;
        }

        ch = use_shift ? scancode_ascii_upper[scancode] : scancode_ascii_lower[scancode];

        if (ch >= ' ' && ch <= '~' && keyboard_idx < 254) {
            keyboard_buffer[keyboard_idx++] = ch;
            keyboard_buffer[keyboard_idx] = '\0';
            keyboard_updated = 1;
        }
    }
}

void init_keyboard(void) {
    /* Enable IRQ1 on the PIC (master PIC mask bit 1) */
    uint16_t mask = pic_get_irq_mask();
    mask &= ~(1 << 1);
    pic_set_irq_mask(mask);

    register_interrupt_handler(IRQ1, keyboard_handler_c);
}

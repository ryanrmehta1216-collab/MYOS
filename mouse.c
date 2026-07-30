#include <stdint.h>
#include "interrupts.h"
#include "serial.h"

/* -----------------------------------------------
 * PS/2 Mouse Driver (IRQ12)
 *
 * Standard PS/2 mouse with 3-byte packet decoding.
 * Handles 9-bit signed deltas for X and Y with
 * proper sign extension, overflow detection, and
 * screen bounds clamping.
 *
 * NOTE on QEMU cursor drift:
 *   PS/2 is a RELATIVE positioning protocol — the
 *   OS accumulates deltas into absolute screen
 *   coordinates. The host cursor and guest cursor
 *   are independent and WILL drift by nature.
 *   For drift-free operation, start QEMU with:
 *     -usbdevice tablet
 *   which uses absolute (tablet) coordinates.
 * ----------------------------------------------- */

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Exported cursor state (volatile: updated by ISR, read by GUI loop) */
volatile int32_t mouse_x = 512;   /* Center of 1024 */
volatile int32_t mouse_y = 384;   /* Center of 768 */
volatile uint8_t mouse_buttons = 0; /* Bit 0=Left, Bit 1=Right, Bit 2=Middle */

/* Internal packet assembly state */
static uint8_t  mouse_cycle = 0;
static uint8_t  mouse_packet[3];

/* Wait for PS/2 controller */
static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        /* Wait for data to be ready to read */
        while ((inb(0x64) & 1) == 0 && --timeout);
    } else {
        /* Wait for controller to be ready to write */
        while ((inb(0x64) & 2) != 0 && --timeout);
    }
}

/* Write a byte to the mouse */
static void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, data);
}

/* Read a byte from the mouse */
static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

/* Forward declaration */
static void mouse_handler_c(registers_t* regs);

/* Initialize the PS/2 mouse hardware */
void init_mouse(void) {
    write_serial("[MOUSE] Initializing PS/2 mouse...\r\n");

    /* 1. Enable the auxiliary device (mouse) */
    mouse_wait(1);
    outb(0x64, 0xA8);

    /* 2. Enable IRQ12 on the PS/2 controller */
    mouse_wait(1);
    outb(0x64, 0x20);              /* Read Command Byte */
    mouse_wait(0);
    uint8_t status = inb(0x60);
    status |= (1 << 1);            /* Enable IRQ12 (bit 1) */
    mouse_wait(1);
    outb(0x64, 0x60);              /* Write Command Byte */
    mouse_wait(1);
    outb(0x60, status);

    /* 3. Set default settings */
    mouse_write(0xF6);
    mouse_read(); /* ACK */

    /* 4. Enable data reporting */
    mouse_write(0xF4);
    mouse_read(); /* ACK */

    /* 5. Enable IRQ12 on the slave PIC (bit 4 of slave mask = IRQ12-8 = bit 4) */
    uint16_t mask = pic_get_irq_mask();
    mask &= ~(1 << 12);  /* Enable IRQ12 (bit 12 in combined mask) */
    pic_set_irq_mask(mask);

    /* Register our handler */
    register_interrupt_handler(IRQ12, mouse_handler_c);

    write_serial("[MOUSE] Mouse initialized.\r\n");
}

/* IRQ12 handler — called from irq_handler via registered handler table.
 *
 * Decodes standard PS/2 3-byte mouse packets:
 *
 *   Byte 0: [Yovfl] [Xovfl] [Ysgn] [Xsgn] [1] [M] [R] [L]
 *           bit 7   bit 6   bit 5  bit 4  bit 3  bit 2 bit 1 bit 0
 *   Byte 1: X delta (8 bits, combined with Xsgn for 9-bit signed)
 *   Byte 2: Y delta (8 bits, combined with Ysgn for 9-bit signed)
 *
 *   X delta = (Xsgn ? (byte1 - 256) : byte1)
 *   Y delta = (Ysgn ? (byte2 - 256) : byte2)
 *   Y is inverted for screen coordinates (+Y = down in VGA).
 *
 *   overflow bits (6,7) checked — packets with overflow are discarded. */
static void mouse_handler_c(registers_t* regs) {
    (void)regs;

    uint8_t status = inb(0x64);

    /* Only process if mouse data is available */
    if (!(status & 0x01)) return;
    if (!(status & 0x20)) return; /* Check auxiliary device bit */

    uint8_t byte = inb(0x60);

    switch (mouse_cycle) {
        case 0:
            /* Byte 0: button flags and overflow bits; sync check */
            /* Bit 3 must be 1 for a valid first packet byte (standard PS/2) */
            if (byte & 0x08) {
                mouse_packet[0] = byte;
                mouse_cycle = 1;
            }
            break;

        case 1:
            /* Byte 1: X delta */
            mouse_packet[1] = byte;
            mouse_cycle = 2;
            break;

        case 2: {
            /* Byte 2: Y delta — packet complete, decode */
            mouse_packet[2] = byte;
            mouse_cycle = 0;

            /* Check for overflow (bits 6-7 of byte 0) */
            if (mouse_packet[0] & 0xC0) break;

            /* Extract button state */
            mouse_buttons = mouse_packet[0] & 0x07;

            /* Decode 9-bit signed X delta:
             *   uint8_t → int32_t (zero-extended)
             *   If X sign bit set: subtract 256 to get negative value */
            int32_t rel_x = (int32_t)mouse_packet[1];
            if (mouse_packet[0] & 0x10)
                rel_x -= 256;

            /* Decode 9-bit signed Y delta */
            int32_t rel_y = (int32_t)mouse_packet[2];
            if (mouse_packet[0] & 0x20)
                rel_y -= 256;

            /* Apply movement with deadzone to prevent sub-pixel drift.
             * In PS/2, values of 0 or ±1 can accumulate over time due to
             * sensor noise. Deadzone of 0 allows all movement through. */
            mouse_x += rel_x;
            mouse_y -= rel_y;  /* Y is inverted for screen coordinates */

            /* Clamp to screen bounds (1024x768).
             * Use signed comparison to prevent overflow wrap-around. */
            if (mouse_x < 0)     mouse_x = 0;
            if (mouse_x >= 1024)  mouse_x = 1023;
            if (mouse_y < 0)     mouse_y = 0;
            if (mouse_y >= 768)   mouse_y = 767;

            break;
        }
    }
}

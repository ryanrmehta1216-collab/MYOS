#include <stdint.h>
#include "serial.h"

/* -----------------------------------------------
 * UART 16550 Serial Driver — COM1
 *
 * Interrupt-driven receive with ring buffer.
 * Combine with Aura-Net for LoRa/Ham radio bridge.
 *
 * I/O port helpers are defined inline here to avoid
 * dependency on a separate I/O header.
 * ----------------------------------------------- */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ==========================================================
 * RX Ring Buffer
 * ========================================================== */
static volatile uint8_t rx_buffer[SERIAL_RX_BUF_SIZE];
static volatile uint32_t rx_head = 0;   /* Producer index (ISR writes here) */
static volatile uint32_t rx_tail = 0;   /* Consumer index (read_serial reads here) */

/* Byte-stuffing frame reassembly state */
#define FRAME_STATE_IDLE    0
#define FRAME_STATE_IN_DATA 1
#define FRAME_STATE_ESC     2

static volatile int frame_state = FRAME_STATE_IDLE;
static volatile uint8_t frame_buffer[SERIAL_MAX_FRAME];
static volatile int frame_pos = 0;
static volatile int frame_complete = 0;

/* ==========================================================
 * Initialization
 * ========================================================== */
void init_serial(void) {
    /* Disable all interrupts while configuring */
    outb(COM1 + UART_IER, 0x00);

    /* Set DLAB to configure baud rate */
    outb(COM1 + UART_LCR, LCR_DLAB);

    /* Set divisor to 3 for 38400 baud (115200 / 3 = 38400) */
    outb(COM1 + UART_TX,  0x03);    /* Low byte */
    outb(COM1 + UART_IER, 0x00);    /* High byte */

    /* 8 bits, no parity, 1 stop bit */
    outb(COM1 + UART_LCR, LCR_8N1);

    /* Enable FIFO, clear RX/TX FIFOs, 14-byte threshold */
    outb(COM1 + UART_FCR, FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX | FCR_TRIGGER_14);

    /* Set RTS/DSR */
    outb(COM1 + UART_MCR, 0x0B);

    /* Enable receive-data-available interrupt */
    outb(COM1 + UART_IER, IER_RX_AVAIL);

    /* Register IRQ4 handler with the interrupt system */
    register_interrupt_handler(IRQ4, serial_irq_handler);

    /* Initialize RX ring buffer */
    rx_head = 0;
    rx_tail = 0;
    frame_state = FRAME_STATE_IDLE;
    frame_pos = 0;
    frame_complete = 0;
}

/* ==========================================================
 * Transmit Functions (Blocking, Polling)
 * ========================================================== */
int is_transmit_empty(void) {
    return inb(COM1 + UART_LSR) & LSR_TX_EMPTY;
}

void write_serial_char(char a) {
    while (is_transmit_empty() == 0);
    outb(COM1 + UART_TX, (uint8_t)a);
}

void write_serial(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        write_serial_char(str[i]);
    }
}

void write_serial_hex(uint32_t n) {
    int tmp;
    char noZeroes = 1;
    write_serial("0x");

    if (n == 0) {
        write_serial_char('0');
        return;
    }

    for (int i = 28; i >= 0; i -= 4) {
        tmp = (n >> i) & 0xF;
        if (tmp == 0 && noZeroes != 0) continue;
        if (tmp >= 0xA) {
            noZeroes = 0;
            write_serial_char(tmp - 0xA + 'A');
        } else {
            noZeroes = 0;
            write_serial_char(tmp + '0');
        }
    }
}

/* ==========================================================
 * IRQ4 — Receive Interrupt Handler
 *
 * Called from the interrupt dispatcher when COM1 receives
 * data. Stores bytes into a ring buffer and also processes
 * the byte-stuffing frame protocol for Aura-Net packets.
 * ========================================================== */
void serial_irq_handler(registers_t* regs) {
    (void)regs;  /* Unused — we just read the UART */
    /* Read all available bytes from the UART */
    while (inb(COM1 + UART_LSR) & LSR_DATA_READY) {
        uint8_t byte = inb(COM1 + UART_RX);

        /* Store in ring buffer */
        uint32_t next_head = (rx_head + 1) % SERIAL_RX_BUF_SIZE;
        if (next_head != rx_tail) {
            rx_buffer[rx_head] = byte;
            rx_head = next_head;
        }

        /* ---- Byte-Stuffing Frame Protocol ----
         * SOF (0xAA) marks the start of a frame
         * EOF (0x55) marks the end of a frame
         * ESC (0xDB) escapes the next byte (value ^ 0x20)
         * This allows binary data to be transmitted safely
         * over the serial link.
         */
        switch (frame_state) {
            case FRAME_STATE_IDLE:
                if (byte == SERIAL_SOF) {
                    frame_state = FRAME_STATE_IN_DATA;
                    frame_pos = 0;
                }
                break;

            case FRAME_STATE_IN_DATA:
                if (byte == SERIAL_EOF) {
                    /* End of frame */
                    frame_complete = 1;
                    frame_state = FRAME_STATE_IDLE;
                } else if (byte == SERIAL_ESC) {
                    /* Next byte is escaped */
                    frame_state = FRAME_STATE_ESC;
                } else if (byte == SERIAL_SOF) {
                    /* Reset — new frame started (abort current) */
                    frame_pos = 0;
                } else {
                    /* Normal data byte */
                    if (frame_pos < SERIAL_MAX_FRAME) {
                        frame_buffer[frame_pos++] = byte;
                    }
                }
                break;

            case FRAME_STATE_ESC:
                /* Un-escape: byte ^ 0x20 */
                if (byte == SERIAL_SOF || byte == SERIAL_EOF || byte == SERIAL_ESC) {
                    if (frame_pos < SERIAL_MAX_FRAME) {
                        frame_buffer[frame_pos++] = byte;
                    }
                } else {
                    /* Invalid escape sequence, discard frame */
                    frame_state = FRAME_STATE_IDLE;
                    frame_pos = 0;
                }
                frame_state = FRAME_STATE_IN_DATA;
                break;
        }
    }
}

/* ==========================================================
 * RX Buffer Access (Polled from main loop / Aura-Net)
 * ========================================================== */
int serial_rx_available(void) {
    /* Number of bytes in ring buffer */
    uint32_t head = rx_head;
    uint32_t tail = rx_tail;
    if (head >= tail) {
        return (int)(head - tail);
    } else {
        return (int)(SERIAL_RX_BUF_SIZE - tail + head);
    }
}

int read_serial_char(void) {
    if (rx_head == rx_tail) return -1;

    uint8_t byte = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % SERIAL_RX_BUF_SIZE;
    return (int)byte;
}

int read_serial(uint8_t* buffer, int max_len) {
    int count = 0;
    while (count < max_len) {
        int c = read_serial_char();
        if (c < 0) break;
        buffer[count++] = (uint8_t)c;
    }
    return count;
}

/* ==========================================================
 * Serial Frame Protocol (Aura-Net Radio Bridge)
 * ========================================================== */

/* Send a framed packet over serial with byte stuffing */
void serial_send_frame(const uint8_t* data, int len) {
    /* Send SOF */
    write_serial_char(SERIAL_SOF);

    for (int i = 0; i < len; i++) {
        uint8_t byte = data[i];
        /* Escape SOF, EOF, and ESC bytes */
        if (byte == SERIAL_SOF || byte == SERIAL_EOF || byte == SERIAL_ESC) {
            write_serial_char(SERIAL_ESC);
            write_serial_char(byte ^ 0x20);
        } else {
            write_serial_char(byte);
        }
    }

    /* Send EOF */
    write_serial_char(SERIAL_EOF);
}

/* Check if a complete frame is available from the byte-stuffing parser */
int serial_frame_available(void) {
    /* The IRQ handler sets frame_complete when it parses a full frame */
    return frame_complete ? 1 : 0;
}

/* Read the last complete frame from the frame buffer.
 * Returns the number of bytes copied, or -1 if no frame available. */
int serial_read_frame(uint8_t* buffer, int max_len) {
    if (!frame_complete) return -1;

    int len = frame_pos;
    if (len > max_len) len = max_len;

    for (int i = 0; i < len; i++) {
        buffer[i] = frame_buffer[i];
    }

    frame_complete = 0;
    frame_pos = 0;
    return len;
}

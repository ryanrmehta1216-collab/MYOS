#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include "interrupts.h"

/* -----------------------------------------------
 * Serial Port (UART 16550) Driver
 *
 * Now with full interrupt-driven receive support
 * for the Aura-Net COM1 Radio Bridge.
 *
 * COM1 @ 0x3F8, IRQ4 (vector 36 after PIC remap)
 * Baud rate: 38400, 8N1, FIFO enabled.
 *
 * The IRQ4 handler stores incoming bytes into a
 * ring buffer. The Aura-Net layer polls this buffer
 * to detect and parse Aura-Net frames sent over radio.
 * ----------------------------------------------- */

/* COM1 base I/O port */
#define COM1            0x3F8
#define COM1_IRQ        4

/* UART registers (offsets from COM1 base, DLAB=0) */
#define UART_RX         0   /* Receive buffer (DLAB=0, read) */
#define UART_TX         0   /* Transmit hold (DLAB=0, write) */
#define UART_IER        1   /* Interrupt Enable Register */
#define UART_IIR        2   /* Interrupt Identification Register (read) */
#define UART_FCR        2   /* FIFO Control Register (write) */
#define UART_LCR        3   /* Line Control Register */
#define UART_MCR        4   /* Modem Control Register */
#define UART_LSR        5   /* Line Status Register */
#define UART_MSR        6   /* Modem Status Register */

/* Interrupt Enable bits */
#define IER_RX_AVAIL    0x01  /* Enable receive data available interrupt */
#define IER_TX_EMPTY    0x02  /* Enable transmit holding register empty interrupt */
#define IER_LINE_STAT   0x04  /* Enable receiver line status interrupt */
#define IER_MODEM_STAT  0x08  /* Enable modem status interrupt */

/* Line Status bits */
#define LSR_DATA_READY  0x01  /* Receive data ready */
#define LSR_TX_EMPTY    0x20  /* Transmit holding register empty */
#define LSR_TRANSMITTER_EMPTY 0x40 /* Transmitter empty */

/* FIFO Control bits */
#define FCR_ENABLE      0x01  /* Enable FIFOs */
#define FCR_CLEAR_RX    0x02  /* Clear receive FIFO */
#define FCR_CLEAR_TX    0x04  /* Clear transmit FIFO */
#define FCR_TRIGGER_14  0xC0  /* Trigger at 14 bytes */

/* Line Control bits */
#define LCR_8N1         0x03  /* 8 bits, no parity, 1 stop bit */
#define LCR_DLAB        0x80  /* Divisor Latch Access Bit */

/* Aura-Net serial frame markers for packet framing */
#define SERIAL_SOF      0xAA  /* Start of Frame marker */
#define SERIAL_EOF      0x55  /* End of Frame marker */
#define SERIAL_ESC      0xDB  /* Escape byte for byte stuffing */

/* Serial RX ring buffer size */
#define SERIAL_RX_BUF_SIZE  4096

/* Maximum serial frame payload */
#define SERIAL_MAX_FRAME    1536

/* Initialize UART 16550 COM1 at 38400 baud, 8N1 */
void init_serial(void);

/* Write a single character (blocking) */
void write_serial_char(char a);

/* Write a null-terminated string (blocking) */
void write_serial(const char* str);

/* Write a 32-bit integer as hex (blocking) */
void write_serial_hex(uint32_t n);

/* Check if transmit buffer is empty */
int is_transmit_empty(void);

/* ---- Interrupt-driven RX functions ---- */

/* Get the number of bytes available in the RX ring buffer */
int serial_rx_available(void);

/* Read a single byte from the RX ring buffer (non-blocking, returns -1 if empty) */
int read_serial_char(void);

/* Read bytes into buffer, returns count read */
int read_serial(uint8_t* buffer, int max_len);

/* IRQ4 handler — called from interrupt handler when COM1 receives data */
void serial_irq_handler(registers_t* regs);

/* ---- Aura-Net Serial Radio Bridge ---- */

/* Send a framed Aura-Net packet over serial (with SOF/EOF/escaping) */
void serial_send_frame(const uint8_t* data, int len);

/* Check if a complete serial frame is available in the RX buffer.
 * Returns: > 0 = frame length ready, 0 = no complete frame yet */
int serial_frame_available(void);

/* Read a complete serial frame from the RX buffer.
 * Returns the number of bytes copied to buffer, or -1 on error.
 * Must call serial_frame_available() first to check. */
int serial_read_frame(uint8_t* buffer, int max_len);

#endif /* SERIAL_H */

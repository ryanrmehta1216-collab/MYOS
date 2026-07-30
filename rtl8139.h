#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

/* -----------------------------------------------
 * RTL8139 Network Driver (Phase 7.2)
 *
 * QEMU emulates an RTL8139 card by default.
 * This driver initializes the card, configures
 * a receive buffer, and provides transmit/receive
 * functions for raw Ethernet frames.
 *
 * The RTL8139 is a simple PCI NIC that uses
 * programmed I/O for register access.
 * ----------------------------------------------- */

/* RTL8139 I/O registers (offsets from BAR0 base) */
#define RTL8139_IDR0        0x00  /* MAC address bytes 0-5 */
#define RTL8139_TSD0        0x10  /* Transmit Status 0 */
#define RTL8139_TSAD0       0x20  /* Transmit Start Address 0 */
#define RTL8139_RBSTART     0x30  /* Receive Buffer Start Address */
#define RTL8139_CR          0x37  /* Command Register */
#define RTL8139_CAPR        0x38  /* Current Address of Packet Read */
#define RTL8139_IMR         0x3C  /* Interrupt Mask Register */
#define RTL8139_ISR         0x3E  /* Interrupt Status Register */
#define RTL8139_TCR         0x40  /* Transmit Configuration Register */
#define RTL8139_RCR         0x44  /* Receive Configuration Register */
#define RTL8139_CONFIG1     0x52  /* Configuration Register 1 */

/* Command Register bits */
#define RTL_CR_RST          0x10  /* Reset */
#define RTL_CR_RE           0x08  /* Receiver Enable */
#define RTL_CR_TE           0x04  /* Transmitter Enable */
#define RTL_CR_BUFE         0x01  /* Buffer Empty */

/* Interrupt bits */
#define RTL_ISR_ROK         0x01  /* Receive OK */
#define RTL_ISR_TOK         0x02  /* Transmit OK */
#define RTL_ISR_RER         0x04  /* Receive Error */
#define RTL_ISR_TER         0x08  /* Transmit Error */
#define RTL_ISR_RXOVW       0x10  /* Receive Overflow */
#define RTL_ISR_SERR        0x8000 /* System Error */

/* Receive buffer size */
#define RX_BUF_SIZE         8192

/* MAC address length */
#define MAC_LENGTH          6

/* Ethernet frame maximum size (standard MTU + headers) */
#define MAX_ETH_FRAME       1518

/* Ethernet frame structure */
typedef struct {
    uint8_t  dst_mac[MAC_LENGTH];
    uint8_t  src_mac[MAC_LENGTH];
    uint16_t ether_type;
    uint8_t  payload[MAX_ETH_FRAME - 14];
} __attribute__((packed)) eth_frame_t;

/* Initialize RTL8139 */
void init_rtl8139(void);

/* Get MAC address */
void rtl8139_get_mac(uint8_t* mac);

/* Send an Ethernet frame */
int rtl8139_send_frame(const uint8_t* data, uint32_t length);

/* Check if a frame has been received. Returns length or 0. */
int rtl8139_poll_frame(uint8_t* buffer, uint32_t max_len);

#endif /* RTL8139_H */

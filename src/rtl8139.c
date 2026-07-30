#include "rtl8139.h"
#include "pci.h"
#include "serial.h"
#include "memory.h"

/* Heap allocator declarations */
extern void* kmalloc(size_t size);

/* -----------------------------------------------
 * RTL8139 Network Driver
 *
 * Initializes the RTL8139 card found via PCI.
 * Provides full send/receive with proper ring
 * buffer parsing.
 *
 * The RTL8139 is the default NIC in QEMU:
 *   - PCI vendor 0x10EC, device 0x8139
 *   - QEMU also emulates e1000 (0x8086, 0x100E)
 *     and virtio-net (0x1AF4, 0x1000)
 * ----------------------------------------------- */

/* I/O port helpers */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t rtl_io_base = 0;
static int rtl_present = 0;
static uint8_t rtl_mac[MAC_LENGTH];
static uint8_t* rx_buffer = NULL;
static uint8_t* rx_buffer_aligned = NULL;

/* RTL8139 also supports e1000 and virtio detection */
static int rtl_try_alternative_nics(void) {
    /* Check for e1000 (Intel PRO/1000 - common QEMU default) */
    int idx = pci_find_device(0x8086, 0x100E);
    if (idx >= 0) {
        write_serial("[RTL8139] Found e1000 at PCI slot ");
        write_serial_hex(pci_devices[idx].slot);
        write_serial(" - RTL8139 driver cannot drive this.\r\n");
        return -1;
    }
    return -1;
}

void init_rtl8139(void) {
    write_serial("[RTL8139] Initializing network card...\r\n");

    int dev_idx = pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE);
    if (dev_idx < 0) {
        write_serial("[RTL8139] No RTL8139 found. QEMU may use a different NIC.\r\n");
        rtl_try_alternative_nics();
        return;
    }

    pci_device_t* dev = &pci_devices[dev_idx];
    rtl_io_base = dev->bar0 & ~0x3;

    write_serial("[RTL8139] I/O base=0x");
    write_serial_hex(rtl_io_base);
    write_serial("\r\n");

    /* Power on the chip */
    uint8_t config1 = inb(rtl_io_base + RTL8139_CONFIG1);
    config1 &= ~(1 << 3);
    outb(rtl_io_base + RTL8139_CONFIG1, config1);

    /* Software reset */
    outb(rtl_io_base + RTL8139_CR, RTL_CR_RST);
    for (int timeout = 0; timeout < 1000; timeout++) {
        if (!(inb(rtl_io_base + RTL8139_CR) & RTL_CR_RST)) break;
    }

    /* Read MAC address */
    for (int i = 0; i < MAC_LENGTH; i++) {
        rtl_mac[i] = inb(rtl_io_base + RTL8139_IDR0 + i);
    }

    write_serial("[RTL8139] MAC: ");
    for (int i = 0; i < MAC_LENGTH; i++) {
        write_serial_hex(rtl_mac[i]);
        if (i < 5) write_serial(":");
    }
    write_serial("\r\n");

    /* Allocate receive buffer (16-byte aligned for RTL8139 requirements) */
    rx_buffer = (uint8_t*)kmalloc(RX_BUF_SIZE + 16);
    if (!rx_buffer) {
        write_serial("[RTL8139] Failed to allocate RX buffer!\r\n");
        return;
    }

    /* 16-byte align the buffer */
    uint32_t rx_addr = (uint32_t)rx_buffer;
    rx_addr = (rx_addr + 15) & ~15;
    rx_buffer_aligned = (uint8_t*)rx_addr;

    /* Zero the buffer */
    for (int i = 0; i < RX_BUF_SIZE; i++) rx_buffer_aligned[i] = 0;

    /* Set receive buffer address */
    outl(rtl_io_base + RTL8139_RBSTART, (uint32_t)rx_buffer_aligned);

    /* Enable interrupts */
    outw(rtl_io_base + RTL8139_IMR, RTL_ISR_ROK | RTL_ISR_TOK);

    /* Configure receive: accept all packets */
    outl(rtl_io_base + RTL8139_RCR, 0xF | (1 << 7));

    /* Enable TX and RX */
    outb(rtl_io_base + RTL8139_CR, RTL_CR_TE | RTL_CR_RE);

    rtl_present = 1;
    write_serial("[RTL8139] Network card initialized.\r\n");
}

void rtl8139_get_mac(uint8_t* mac) {
    for (int i = 0; i < MAC_LENGTH; i++) {
        mac[i] = rtl_mac[i];
    }
}

int rtl8139_send_frame(const uint8_t* data, uint32_t length) {
    if (!rtl_present) return -1;
    if (length > MAX_ETH_FRAME) return -2;

    uint32_t status = inl(rtl_io_base + RTL8139_TSD0);
    if (status & 0x8000) return -3;

    outl(rtl_io_base + RTL8139_TSAD0, (uint32_t)data);
    outl(rtl_io_base + RTL8139_TSD0, length | 0x10000);

    return 0;
}

/* Poll for a received frame by parsing the RTL8139 ring buffer.
 * RTL8139 ring buffer format:
 *   Each received packet starts with a 4-byte header:
 *     [15:0]  = packet length (including 4-byte header)
 *     [31:16] = status (bit 0 = ROK)
 *   Followed by the packet data (padded to 4 bytes).
 */
int rtl8139_poll_frame(uint8_t* buffer, uint32_t max_len) {
    if (!rtl_present) return 0;
    if (!rx_buffer_aligned) return 0;

    /* Read the current buffer position from CAPR */
    uint16_t capr = inw(rtl_io_base + RTL8139_CAPR);
    uint32_t offset = capr % (RX_BUF_SIZE - 4);

    /* Check packet header at the current ring buffer offset */
    uint16_t* header = (uint16_t*)(rx_buffer_aligned + offset);
    uint16_t pkt_len_raw = header[0];
    uint16_t pkt_status = header[1];

    /* Bit 0 of status = ROK (Receive OK) */
    if (!(pkt_status & 0x0001)) return 0;

    /* Get the actual packet length (exclude 4-byte header) */
    uint16_t pkt_len = pkt_len_raw & 0x3FFF;
    if (pkt_len < 4 || pkt_len > max_len + 4) {
        outw(rtl_io_base + RTL8139_CAPR, (offset + 4) % (RX_BUF_SIZE - 4));
        return 0;
    }

    uint16_t data_len = pkt_len - 4;

    /* Copy the packet data from the aligned buffer */
    uint8_t* src = rx_buffer_aligned + offset + 4;
    for (uint16_t i = 0; i < data_len && i < max_len; i++) {
        buffer[i] = src[i];
    }

    /* Update CAPR to tell the card we've consumed this packet */
    uint16_t new_capr = (offset + pkt_len + 4) & ~3;
    new_capr %= (RX_BUF_SIZE - 4);
    outw(rtl_io_base + RTL8139_CAPR, new_capr);

    outw(rtl_io_base + RTL8139_ISR, RTL_ISR_ROK);
    return data_len;
}

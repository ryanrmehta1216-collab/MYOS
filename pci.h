#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* -----------------------------------------------
 * PCI Bus Enumeration (Phase 7.1)
 *
 * Scan the PCI bus to discover devices, then
 * locate the RTL8139 network card.
 *
 * PCI config space is accessed via I/O ports
 * 0xCF8 (address) and 0xCFC (data).
 * ----------------------------------------------- */

/* PCI config I/O ports */
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

/* RTL8139 vendor/device IDs */
#define RTL8139_VENDOR   0x10EC
#define RTL8139_DEVICE   0x8139

/* PCI device information */
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint32_t bar0;  /* Base Address Register 0 */
    uint32_t bar1;
} pci_device_t;

/* Maximum PCI devices to scan */
#define MAX_PCI_DEVICES 32

/* Global array of discovered PCI devices */
extern pci_device_t pci_devices[MAX_PCI_DEVICES];
extern int pci_device_count;

/* Initialize PCI bus enumeration */
void init_pci(void);

/* Read a 32-bit value from PCI config space */
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/* Find a PCI device by vendor/device ID. Returns index or -1. */
int pci_find_device(uint16_t vendor, uint16_t device);

#endif /* PCI_H */

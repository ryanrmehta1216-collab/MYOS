#include "pci.h"
#include "serial.h"

/* -----------------------------------------------
 * PCI Bus Enumeration
 *
 * Uses standard PCI configuration mechanism #1
 * via I/O ports 0xCF8/0xCFC.
 *
 * Scans bus 0 for all devices. If a PCI-to-PCI
 * bridge is found (class 0x06, subclass 0x04),
 * the secondary bus is scanned as well.
 * ----------------------------------------------- */

/* Global PCI device table */
pci_device_t pci_devices[MAX_PCI_DEVICES];
int pci_device_count = 0;

/* I/O port helpers */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Read a 32-bit value from PCI config space */
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

/* Add a discovered PCI device to the table */
static void add_device(uint8_t bus, uint8_t slot, uint8_t func,
                       uint16_t vendor, uint16_t device,
                       uint32_t class_rev, uint32_t bar0, uint32_t bar1) {
    if (pci_device_count >= MAX_PCI_DEVICES) return;

    pci_device_t* dev = &pci_devices[pci_device_count];
    dev->vendor_id = vendor;
    dev->device_id = device;
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->class_code = (class_rev >> 24) & 0xFF;
    dev->subclass = (class_rev >> 16) & 0xFF;
    dev->prog_if = (class_rev >> 8) & 0xFF;
    dev->bar0 = bar0;
    dev->bar1 = bar1;

    pci_device_count++;

    write_serial("[PCI] Found vendor=0x");
    write_serial_hex(vendor);
    write_serial(" dev=0x");
    write_serial_hex(device);
    write_serial(" class=0x");
    write_serial_hex(dev->class_code);
    write_serial(" bus=");
    write_serial_hex(bus);
    write_serial(" slot=");
    write_serial_hex(slot);
    write_serial("\r\n");
}

/* Maximum PCI bus scan depth to prevent infinite recursion */
#define PCI_MAX_DEPTH 8

/* Scan a single PCI bus for devices at the given recursion depth */
static void scan_bus_depth(uint8_t bus, int depth) {
    if (depth > PCI_MAX_DEPTH) {
        write_serial("[PCI] Max bus depth reached, stopping.\r\n");
        return;
    }
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t vendor_device = pci_read_config(bus, slot, 0, 0);
        uint16_t vendor_id = vendor_device & 0xFFFF;
        uint16_t device_id = (vendor_device >> 16) & 0xFFFF;

        if (vendor_id == 0xFFFF) continue;

        /* Check for multi-function device (bit 23 of header type) */
        uint32_t header_type = pci_read_config(bus, slot, 0, 0x0C);
        int max_func = (header_type & 0x800000) ? 8 : 1;

        for (uint8_t func = 0; func < max_func; func++) {
            if (func > 0) {
                vendor_device = pci_read_config(bus, slot, func, 0);
                vendor_id = vendor_device & 0xFFFF;
                device_id = (vendor_device >> 16) & 0xFFFF;
                if (vendor_id == 0xFFFF) continue;
            }

            uint32_t class_rev = pci_read_config(bus, slot, func, 0x08);
            uint32_t bar0 = pci_read_config(bus, slot, func, 0x10);
            uint32_t bar1 = pci_read_config(bus, slot, func, 0x14);

            add_device(bus, slot, func, vendor_id, device_id, class_rev, bar0, bar1);

            /* If this is a PCI-to-PCI bridge, scan the secondary bus */
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass = (class_rev >> 16) & 0xFF;
            if (class_code == 0x06 && subclass == 0x04) {
                uint32_t sec_bus_reg = pci_read_config(bus, slot, func, 0x18);
                uint8_t secondary_bus = (sec_bus_reg >> 8) & 0xFF;
                write_serial("[PCI] Bridge found, scanning bus ");
                write_serial_hex(secondary_bus);
                write_serial("\r\n");
                scan_bus_depth(secondary_bus, depth + 1);
            }
        }
    }
}    /* Initialize PCI subsystem */
void init_pci(void) {
    write_serial("[PCI] Scanning PCI bus...\r\n");

    pci_device_count = 0;

    /* Scan bus 0 and any bridged buses */
    scan_bus_depth(0, 0);

    write_serial("[PCI] Scan complete: ");
    write_serial_hex(pci_device_count);
    write_serial(" devices found.\r\n");
}

/* Find a PCI device by vendor/device ID. Returns index or -1. */
int pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor && pci_devices[i].device_id == device) {
            write_serial("[PCI] Found RTL8139 at bus ");
            write_serial_hex(pci_devices[i].bus);
            write_serial(" slot ");
            write_serial_hex(pci_devices[i].slot);
            write_serial("\r\n");
            return i;
        }
    }
    return -1;
}

/*
 * Realtek RTL8139 Fast Ethernet driver (QEMU `-device rtl8139`, many
 * older PCs). Port-I/O based, polling, 8 KiB receive ring, 4 TX slots.
 */
#include "perwix.h"
#include "netdev.h"

#define REG_MAC 0x00
#define REG_TSD0 0x10
#define REG_TSAD0 0x20
#define REG_RBSTART 0x30
#define REG_CR 0x37
#define REG_CAPR 0x38
#define REG_IMR 0x3c
#define REG_TCR 0x40
#define REG_RCR 0x44
#define REG_CONFIG1 0x52
#define REG_MSR 0x58

#define RX_RING_SIZE 8192
#define RX_BUFFER_PAGES 3          /* 8 KiB ring + 16 + 1500 wrap slack */

static uint16_t io;
static uint8_t *rx_ring;
static uint8_t *tx_area;           /* 4 slots of 2 KiB */
static uint32_t rx_offset;
static uint32_t tx_slot;

static bool rtl8139_match(uint16_t vendor, uint16_t device)
{
    return vendor == 0x10ec && device == 0x8139;
}

static bool rtl8139_send(netdev_t *dev, const uint8_t *frame, uint16_t length)
{
    (void)dev;
    if (length > 1792) {
        return false;
    }
    uint8_t *slot = tx_area + tx_slot * 2048;
    for (uint32_t i = 0; i < length; ++i) {
        slot[i] = frame[i];
    }
    uint16_t sent = length < 60 ? 60 : length;
    outl((uint16_t)(io + REG_TSAD0 + tx_slot * 4), (uint32_t)(uintptr_t)slot);
    outl((uint16_t)(io + REG_TSD0 + tx_slot * 4), sent);   /* clears OWN */
    uint32_t status = 0;
    for (uint32_t w = 0; w < 1000000; ++w) {
        status = inl((uint16_t)(io + REG_TSD0 + tx_slot * 4));
        if ((status & 0x8000U) != 0 || (status & 0x40000000U) != 0) {
            break;                                         /* TOK / TABT */
        }
    }
    tx_slot = (tx_slot + 1) & 3;
    return (status & 0x8000U) != 0;
}

static uint16_t rtl8139_receive(netdev_t *dev, uint8_t *buffer, uint16_t capacity)
{
    (void)dev;
    if ((inb((uint16_t)(io + REG_CR)) & 1U) != 0) {
        return 0;                                          /* buffer empty */
    }
    const uint8_t *header = rx_ring + rx_offset;
    uint16_t status = (uint16_t)(header[0] | (header[1] << 8));
    uint16_t size = (uint16_t)(header[2] | (header[3] << 8));
    uint16_t length = 0;
    if ((status & 1U) != 0 && size >= 4 && (uint16_t)(size - 4) <= capacity) {
        length = (uint16_t)(size - 4);                     /* drop CRC */
        for (uint16_t i = 0; i < length; ++i) {
            buffer[i] = header[4 + i];
        }
    }
    /* Advance the read pointer (dword aligned) and tell the chip. */
    rx_offset = (rx_offset + size + 4 + 3) & ~3U;
    if (rx_offset >= RX_RING_SIZE) {
        rx_offset -= RX_RING_SIZE;
    }
    outw((uint16_t)(io + REG_CAPR), (uint16_t)(rx_offset - 16));
    if ((status & 1U) == 0 || size < 4) {
        /* Corrupt header: resynchronise by resetting the receiver. */
        uint8_t cr = inb((uint16_t)(io + REG_CR));
        outb((uint16_t)(io + REG_CR), (uint8_t)(cr & ~0x08U));
        outb((uint16_t)(io + REG_CR), cr);
        outl((uint16_t)(io + REG_RCR), 0x8e);
        rx_offset = 0;
    }
    return length;
}

static bool rtl8139_link_up(netdev_t *dev)
{
    (void)dev;
    return (inb((uint16_t)(io + REG_MSR)) & 4U) == 0;
}

static bool rtl8139_init(uint8_t bus, uint8_t device, uint8_t function,
                         netdev_t *dev)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x10);
    if ((bar & 1U) == 0) {
        return false;
    }
    io = (uint16_t)(bar & ~3U);
    nic_pci_enable(bus, device, function);

    outb((uint16_t)(io + REG_CONFIG1), 0);              /* power on */
    outb((uint16_t)(io + REG_CR), 0x10);                /* software reset */
    bool reset_done = false;
    for (uint32_t w = 0; w < 1000000; ++w) {
        if ((inb((uint16_t)(io + REG_CR)) & 0x10U) == 0) {
            reset_done = true;
            break;
        }
    }
    if (!reset_done) {
        return false;
    }

    rx_ring = nic_alloc_dma(RX_BUFFER_PAGES);
    tx_area = nic_alloc_dma(2);
    if (rx_ring == NULL || tx_area == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < 6; ++i) {
        dev->mac[i] = inb((uint16_t)(io + REG_MAC + i));
    }

    outl((uint16_t)(io + REG_RBSTART), (uint32_t)(uintptr_t)rx_ring);
    outw((uint16_t)(io + REG_IMR), 0);                  /* polling only */
    /* Accept broadcast, multicast, own unicast; WRAP so frames at the
     * end of the ring stay contiguous. */
    outl((uint16_t)(io + REG_RCR), 0x8e);
    outb((uint16_t)(io + REG_CR), 0x0c);                /* RX + TX enable */
    outl((uint16_t)(io + REG_TCR), 0x03000700);
    rx_offset = 0;
    tx_slot = 0;

    dev->send = rtl8139_send;
    dev->receive = rtl8139_receive;
    dev->link_up = rtl8139_link_up;
    return true;
}

const nic_driver_t nic_driver_rtl8139 = {
    .name = "rtl8139",
    .match = rtl8139_match,
    .init = rtl8139_init,
};

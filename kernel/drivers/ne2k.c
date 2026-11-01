/*
 * NE2000-compatible PCI NIC (Realtek RTL8029, QEMU `-device ne2k_pci`).
 * DP8390 core with 16-bit remote DMA through the data port. Polling.
 */
#include "perwix.h"
#include "netdev.h"

#define NE_CR 0x00
#define NE_PSTART 0x01      /* page 0 write */
#define NE_PSTOP 0x02
#define NE_BNRY 0x03
#define NE_TPSR 0x04
#define NE_TBCR0 0x05
#define NE_TBCR1 0x06
#define NE_ISR 0x07
#define NE_RSAR0 0x08
#define NE_RSAR1 0x09
#define NE_RBCR0 0x0a
#define NE_RBCR1 0x0b
#define NE_RCR 0x0c
#define NE_TCR 0x0d
#define NE_DCR 0x0e
#define NE_IMR 0x0f
#define NE_PAR0 0x01        /* page 1 */
#define NE_CURR 0x07
#define NE_DATA 0x10
#define NE_RESET 0x1f

#define TX_PAGE 0x40
#define RX_START 0x46
#define RX_STOP 0x80

static uint16_t io;
static uint8_t next_packet = RX_START;

static void select_page(uint8_t page)
{
    outb((uint16_t)(io + NE_CR), (uint8_t)(0x22 | (page << 6)));
}

/* Remote DMA read of `length` bytes from NIC memory `address`. */
static void dma_read(uint16_t address, uint8_t *data, uint16_t length)
{
    select_page(0);
    outb((uint16_t)(io + NE_RBCR0), (uint8_t)length);
    outb((uint16_t)(io + NE_RBCR1), (uint8_t)(length >> 8));
    outb((uint16_t)(io + NE_RSAR0), (uint8_t)address);
    outb((uint16_t)(io + NE_RSAR1), (uint8_t)(address >> 8));
    outb((uint16_t)(io + NE_CR), 0x0a);             /* remote read, start */
    for (uint16_t i = 0; i < length; i += 2) {
        uint16_t word = inw((uint16_t)(io + NE_DATA));
        data[i] = (uint8_t)word;
        if (i + 1 < length) {
            data[i + 1] = (uint8_t)(word >> 8);
        }
    }
}

static bool ne2k_match(uint16_t vendor, uint16_t device)
{
    return vendor == 0x10ec && device == 0x8029;
}

static bool ne2k_send(netdev_t *dev, const uint8_t *frame, uint16_t length)
{
    (void)dev;
    if (length > 1514) {
        return false;
    }
    uint16_t size = length < 60 ? 60 : length;
    select_page(0);
    outb((uint16_t)(io + NE_ISR), 0x40);            /* clear remote-DMA done */
    outb((uint16_t)(io + NE_RBCR0), (uint8_t)size);
    outb((uint16_t)(io + NE_RBCR1), (uint8_t)(size >> 8));
    outb((uint16_t)(io + NE_RSAR0), 0);
    outb((uint16_t)(io + NE_RSAR1), TX_PAGE);
    outb((uint16_t)(io + NE_CR), 0x12);             /* remote write, start */
    for (uint16_t i = 0; i < size; i += 2) {
        uint16_t low = i < length ? frame[i] : 0;
        uint16_t high = i + 1 < length ? frame[i + 1] : 0;
        outw((uint16_t)(io + NE_DATA), (uint16_t)(low | (high << 8)));
    }
    for (uint32_t w = 0; w < 100000; ++w) {
        if ((inb((uint16_t)(io + NE_ISR)) & 0x40U) != 0) {
            break;
        }
    }
    outb((uint16_t)(io + NE_ISR), 0x40);
    outb((uint16_t)(io + NE_TPSR), TX_PAGE);
    outb((uint16_t)(io + NE_TBCR0), (uint8_t)size);
    outb((uint16_t)(io + NE_TBCR1), (uint8_t)(size >> 8));
    outb((uint16_t)(io + NE_CR), 0x26);             /* transmit */
    for (uint32_t w = 0; w < 1000000; ++w) {
        if ((inb((uint16_t)(io + NE_ISR)) & 0x0aU) != 0) {   /* PTX | TXE */
            break;
        }
    }
    bool sent = (inb((uint16_t)(io + NE_ISR)) & 0x02U) != 0;
    outb((uint16_t)(io + NE_ISR), 0x0a);
    return sent;
}

static uint16_t ne2k_receive(netdev_t *dev, uint8_t *buffer, uint16_t capacity)
{
    (void)dev;
    /* Receive-buffer overflow: clear it and restart reception. */
    if ((inb((uint16_t)(io + NE_ISR)) & 0x10U) != 0) {
        outb((uint16_t)(io + NE_ISR), 0x10);
        outb((uint16_t)(io + NE_BNRY), (uint8_t)(next_packet == RX_START ?
                                                 RX_STOP - 1 : next_packet - 1));
    }
    select_page(1);
    uint8_t current = inb((uint16_t)(io + NE_CURR));
    select_page(0);
    if (current == next_packet) {
        return 0;
    }

    uint8_t header[4];
    dma_read((uint16_t)(next_packet << 8), header, 4);
    uint8_t following = header[1];
    uint16_t total = (uint16_t)(header[2] | (header[3] << 8));
    uint16_t length = 0;
    bool valid = (header[0] & 1U) != 0 && total > 8 && total <= 1536 &&
        following >= RX_START && following < RX_STOP;
    if (valid && (uint16_t)(total - 4) <= capacity) {
        length = (uint16_t)(total - 4);
        uint16_t address = (uint16_t)((next_packet << 8) + 4);
        uint16_t ring_end = (uint16_t)(RX_STOP << 8);
        uint16_t first = length;
        if (address + first > ring_end) {            /* wraps around ring */
            first = (uint16_t)(ring_end - address);
        }
        dma_read(address, buffer, first);
        if (first < length) {
            dma_read((uint16_t)(RX_START << 8), buffer + first,
                     (uint16_t)(length - first));
        }
    }
    next_packet = valid ? following : current;       /* resync on bad header */
    outb((uint16_t)(io + NE_BNRY), (uint8_t)(next_packet == RX_START ?
                                             RX_STOP - 1 : next_packet - 1));
    return length;
}

static bool ne2k_link_up(netdev_t *dev)
{
    (void)dev;
    return true;
}

static bool ne2k_init(uint8_t bus, uint8_t device, uint8_t function,
                      netdev_t *dev)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x10);
    if ((bar & 1U) == 0) {
        return false;
    }
    io = (uint16_t)(bar & ~3U);
    nic_pci_enable(bus, device, function);

    outb((uint16_t)(io + NE_RESET), inb((uint16_t)(io + NE_RESET)));
    bool reset_done = false;
    for (uint32_t w = 0; w < 1000000; ++w) {
        if ((inb((uint16_t)(io + NE_ISR)) & 0x80U) != 0) {
            reset_done = true;
            break;
        }
    }
    if (!reset_done) {
        return false;
    }
    outb((uint16_t)(io + NE_ISR), 0xff);

    outb((uint16_t)(io + NE_CR), 0x21);             /* stop, page 0 */
    outb((uint16_t)(io + NE_DCR), 0x49);            /* word-wide DMA */
    outb((uint16_t)(io + NE_RBCR0), 0);
    outb((uint16_t)(io + NE_RBCR1), 0);
    outb((uint16_t)(io + NE_RCR), 0x20);            /* monitor */
    outb((uint16_t)(io + NE_TCR), 0x02);            /* internal loopback */

    uint8_t prom[32];
    dma_read(0, prom, 32);
    for (uint32_t i = 0; i < 6; ++i) {
        dev->mac[i] = prom[i * 2];                  /* each byte doubled */
    }

    outb((uint16_t)(io + NE_CR), 0x21);
    outb((uint16_t)(io + NE_PSTART), RX_START);
    outb((uint16_t)(io + NE_PSTOP), RX_STOP);
    outb((uint16_t)(io + NE_BNRY), RX_START);
    outb((uint16_t)(io + NE_TPSR), TX_PAGE);
    outb((uint16_t)(io + NE_ISR), 0xff);
    outb((uint16_t)(io + NE_IMR), 0);               /* polling only */
    select_page(1);
    outb((uint16_t)(io + NE_CR), 0x61);
    for (uint32_t i = 0; i < 6; ++i) {
        outb((uint16_t)(io + NE_PAR0 + i), dev->mac[i]);
    }
    outb((uint16_t)(io + NE_CURR), RX_START + 1);
    outb((uint16_t)(io + NE_CR), 0x22);             /* start, page 0 */
    outb((uint16_t)(io + NE_TCR), 0x00);            /* normal transmit */
    outb((uint16_t)(io + NE_RCR), 0x04);            /* accept broadcast */
    next_packet = RX_START + 1;

    dev->send = ne2k_send;
    dev->receive = ne2k_receive;
    dev->link_up = ne2k_link_up;
    return true;
}

const nic_driver_t nic_driver_ne2k = {
    .name = "ne2k-pci",
    .match = ne2k_match,
    .init = ne2k_init,
};

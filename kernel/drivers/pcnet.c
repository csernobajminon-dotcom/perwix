/*
 * AMD PCnet-PCI II (Am79C970A / 79C973), as emulated by QEMU `-device pcnet`
 * and VMware/VirtualBox "PCnet-FAST". 32-bit (DWIO) port access and
 * 32-bit descriptor style (SWSTYLE 2).
 */
#include "perwix.h"
#include "netdev.h"

#define RDP 0x10
#define RAP 0x14
#define RESET 0x18
#define BDP 0x1c

#define RX_COUNT 8
#define TX_COUNT 4
#define BUFFER_SIZE 2048

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t address;
    uint32_t flags;                 /* OWN/ERR/STP/ENP + 2's complement BCNT */
    uint32_t message_length;
    uint32_t reserved;
} pcnet_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint8_t rx_length;              /* log2(ring size) << 4 */
    uint8_t tx_length;
    uint8_t mac[6];
    uint16_t reserved;
    uint8_t filter[8];
    uint32_t rx_ring;
    uint32_t tx_ring;
} pcnet_init_t;

static uint16_t io;
static pcnet_desc_t *rx_ring;
static pcnet_desc_t *tx_ring;
static uint8_t *rx_buffers;
static uint8_t *tx_buffers;
static uint32_t rx_next;
static uint32_t tx_next;

static uint32_t csr_read(uint32_t index)
{
    outl((uint16_t)(io + RAP), index);
    return inl((uint16_t)(io + RDP));
}

static void csr_write(uint32_t index, uint32_t value)
{
    outl((uint16_t)(io + RAP), index);
    outl((uint16_t)(io + RDP), value);
}

static void bcr_write(uint32_t index, uint32_t value)
{
    outl((uint16_t)(io + RAP), index);
    outl((uint16_t)(io + BDP), value);
}

static bool pcnet_match(uint16_t vendor, uint16_t device)
{
    return vendor == 0x1022 && device == 0x2000;
}

static bool pcnet_send(netdev_t *dev, const uint8_t *frame, uint16_t length)
{
    (void)dev;
    pcnet_desc_t *d = &tx_ring[tx_next];
    for (uint32_t w = 0; (d->flags & 0x80000000U) != 0 && w < 1000000; ++w) {
        __asm__ volatile("pause");
    }
    if ((d->flags & 0x80000000U) != 0 || length > BUFFER_SIZE) {
        return false;
    }
    uint8_t *to = tx_buffers + tx_next * BUFFER_SIZE;
    for (uint32_t i = 0; i < length; ++i) {
        to[i] = frame[i];
    }
    d->flags = 0x80000000U | 0x03000000U | 0xf000U |
        ((uint32_t)(-(int32_t)length) & 0xfffU);
    tx_next = (tx_next + 1) % TX_COUNT;
    csr_write(0, 0x0008 | 0x0002);                  /* TDMD (+ keep STRT) */
    for (uint32_t w = 0; (d->flags & 0x80000000U) != 0 && w < 1000000; ++w) {
        __asm__ volatile("pause");
    }
    return (d->flags & 0x80000000U) == 0;
}

static uint16_t pcnet_receive(netdev_t *dev, uint8_t *buffer, uint16_t capacity)
{
    (void)dev;
    pcnet_desc_t *d = &rx_ring[rx_next];
    if ((d->flags & 0x80000000U) != 0) {
        return 0;                                   /* still owned by chip */
    }
    uint16_t length = 0;
    uint32_t size = d->message_length & 0xfffU;
    if ((d->flags & 0x40000000U) == 0 && size > 4 && size - 4 <= capacity) {
        length = (uint16_t)(size - 4);
        const uint8_t *from = rx_buffers + rx_next * BUFFER_SIZE;
        for (uint16_t i = 0; i < length; ++i) {
            buffer[i] = from[i];
        }
    }
    d->message_length = 0;
    d->flags = 0x80000000U | 0xf000U |
        ((uint32_t)(-(int32_t)BUFFER_SIZE) & 0xfffU);
    rx_next = (rx_next + 1) % RX_COUNT;
    return length;
}

static bool pcnet_link_up(netdev_t *dev)
{
    (void)dev;
    return true;
}

static bool pcnet_init(uint8_t bus, uint8_t device, uint8_t function,
                       netdev_t *dev)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x10);
    if ((bar & 1U) == 0) {
        return false;
    }
    io = (uint16_t)(bar & ~3U);
    nic_pci_enable(bus, device, function);

    for (uint32_t i = 0; i < 6; ++i) {
        dev->mac[i] = inb((uint16_t)(io + i));      /* address PROM */
    }
    (void)inl((uint16_t)(io + RESET));              /* 32-bit reset */
    nic_delay(100000);
    outl((uint16_t)(io + RDP), 0);                  /* latch DWIO mode */
    bcr_write(20, 2);                               /* SWSTYLE = 2 (32-bit) */

    rx_ring = nic_alloc_dma(1);
    tx_ring = nic_alloc_dma(1);
    rx_buffers = nic_alloc_dma(RX_COUNT * BUFFER_SIZE / 4096);
    tx_buffers = nic_alloc_dma(TX_COUNT * BUFFER_SIZE / 4096);
    pcnet_init_t *block = nic_alloc_dma(1);
    if (rx_ring == NULL || tx_ring == NULL || rx_buffers == NULL ||
        tx_buffers == NULL || block == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < RX_COUNT; ++i) {
        rx_ring[i].address = (uint32_t)(uintptr_t)(rx_buffers + i * BUFFER_SIZE);
        rx_ring[i].flags = 0x80000000U | 0xf000U |
            ((uint32_t)(-(int32_t)BUFFER_SIZE) & 0xfffU);
    }
    for (uint32_t i = 0; i < TX_COUNT; ++i) {
        tx_ring[i].address = (uint32_t)(uintptr_t)(tx_buffers + i * BUFFER_SIZE);
    }
    block->mode = 0;
    block->rx_length = 3 << 4;                      /* 2^3 = 8 */
    block->tx_length = 2 << 4;                      /* 2^2 = 4 */
    for (uint32_t i = 0; i < 6; ++i) {
        block->mac[i] = dev->mac[i];
    }
    block->rx_ring = (uint32_t)(uintptr_t)rx_ring;
    block->tx_ring = (uint32_t)(uintptr_t)tx_ring;

    csr_write(1, (uint32_t)(uintptr_t)block & 0xffff);
    csr_write(2, (uint32_t)((uintptr_t)block >> 16));
    csr_write(0, 0x0001);                           /* INIT */
    bool done = false;
    for (uint32_t w = 0; w < 1000000; ++w) {
        if ((csr_read(0) & 0x0100U) != 0) {         /* IDON */
            done = true;
            break;
        }
    }
    if (!done) {
        return false;
    }
    csr_write(0, 0x0002);                           /* STRT */
    rx_next = 0;
    tx_next = 0;

    dev->send = pcnet_send;
    dev->receive = pcnet_receive;
    dev->link_up = pcnet_link_up;
    return true;
}

const nic_driver_t nic_driver_pcnet = {
    .name = "pcnet32",
    .match = pcnet_match,
    .init = pcnet_init,
};

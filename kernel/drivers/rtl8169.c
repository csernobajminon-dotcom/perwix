/*
 * Realtek RTL8169 / RTL8168 / RTL8111 Gigabit Ethernet (very common on
 * real PCs). Uses the I/O-port BAR and descriptor rings; polling.
 *
 * EXPERIMENTAL: not verified on physical hardware. Newer chip revisions
 * (RTL8168 "H" and later) may need PHY/firmware setup that is not done.
 */
#include "perwix.h"
#include "netdev.h"

#define REG_MAC 0x00
#define REG_TNPDS 0x20
#define REG_CR 0x37
#define REG_TPPOLL 0x38
#define REG_IMR 0x3c
#define REG_ISR 0x3e
#define REG_TCR 0x40
#define REG_RCR 0x44
#define REG_9346CR 0x50
#define REG_PHYSTATUS 0x6c
#define REG_RMS 0xda
#define REG_RDSAR 0xe4
#define REG_ETTHR 0xec

#define RX_COUNT 16
#define TX_COUNT 4
#define BUFFER_SIZE 2048

#define DESC_OWN 0x80000000U
#define DESC_EOR 0x40000000U
#define DESC_FS 0x20000000U
#define DESC_LS 0x10000000U

typedef struct __attribute__((packed, aligned(16))) {
    uint32_t opts1;
    uint32_t opts2;
    uint32_t address_low;
    uint32_t address_high;
} rtl_desc_t;

static uint16_t io;
static rtl_desc_t *rx_ring;
static rtl_desc_t *tx_ring;
static uint8_t *rx_buffers;
static uint8_t *tx_buffers;
static uint32_t rx_next;
static uint32_t tx_next;

static bool rtl8169_match(uint16_t vendor, uint16_t device)
{
    return vendor == 0x10ec &&
        (device == 0x8169 || device == 0x8168 || device == 0x8167 ||
         device == 0x8136);
}

static void arm_rx(uint32_t index)
{
    rx_ring[index].opts2 = 0;
    rx_ring[index].opts1 = DESC_OWN | BUFFER_SIZE |
        (index == RX_COUNT - 1 ? DESC_EOR : 0);
}

static bool rtl8169_send(netdev_t *dev, const uint8_t *frame, uint16_t length)
{
    (void)dev;
    rtl_desc_t *d = &tx_ring[tx_next];
    for (uint32_t w = 0; (d->opts1 & DESC_OWN) != 0 && w < 1000000; ++w) {
        __asm__ volatile("pause");
    }
    if ((d->opts1 & DESC_OWN) != 0 || length > BUFFER_SIZE) {
        return false;
    }
    uint8_t *to = tx_buffers + tx_next * BUFFER_SIZE;
    for (uint32_t i = 0; i < length; ++i) {
        to[i] = frame[i];
    }
    d->opts2 = 0;
    d->opts1 = DESC_OWN | DESC_FS | DESC_LS | length |
        (tx_next == TX_COUNT - 1 ? DESC_EOR : 0);
    tx_next = (tx_next + 1) % TX_COUNT;
    outb((uint16_t)(io + REG_TPPOLL), 0x40);        /* normal-priority poll */
    for (uint32_t w = 0; (d->opts1 & DESC_OWN) != 0 && w < 1000000; ++w) {
        __asm__ volatile("pause");
    }
    return (d->opts1 & DESC_OWN) == 0;
}

static uint16_t rtl8169_receive(netdev_t *dev, uint8_t *buffer, uint16_t capacity)
{
    (void)dev;
    rtl_desc_t *d = &rx_ring[rx_next];
    if ((d->opts1 & DESC_OWN) != 0) {
        return 0;
    }
    uint32_t size = d->opts1 & 0x3fffU;
    uint16_t length = 0;
    bool error = (d->opts1 & (1U << 21)) != 0;       /* RES: receive error */
    if (!error && size > 4 && size - 4 <= capacity) {
        length = (uint16_t)(size - 4);               /* drop CRC */
        const uint8_t *from = rx_buffers + rx_next * BUFFER_SIZE;
        for (uint16_t i = 0; i < length; ++i) {
            buffer[i] = from[i];
        }
    }
    arm_rx(rx_next);
    rx_next = (rx_next + 1) % RX_COUNT;
    return length;
}

static bool rtl8169_link_up(netdev_t *dev)
{
    (void)dev;
    return (inb((uint16_t)(io + REG_PHYSTATUS)) & 2U) != 0;
}

static bool rtl8169_init(uint8_t bus, uint8_t device, uint8_t function,
                         netdev_t *dev)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x10);
    if ((bar & 1U) == 0) {
        return false;
    }
    io = (uint16_t)(bar & ~3U);
    nic_pci_enable(bus, device, function);

    outb((uint16_t)(io + REG_CR), 0x10);            /* reset */
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

    rx_ring = nic_alloc_dma(1);
    tx_ring = nic_alloc_dma(1);
    rx_buffers = nic_alloc_dma(RX_COUNT * BUFFER_SIZE / 4096);
    tx_buffers = nic_alloc_dma(TX_COUNT * BUFFER_SIZE / 4096);
    if (rx_ring == NULL || tx_ring == NULL || rx_buffers == NULL ||
        tx_buffers == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < RX_COUNT; ++i) {
        uint64_t address = (uint64_t)(uintptr_t)(rx_buffers + i * BUFFER_SIZE);
        rx_ring[i].address_low = (uint32_t)address;
        rx_ring[i].address_high = (uint32_t)(address >> 32);
        arm_rx(i);
    }
    for (uint32_t i = 0; i < TX_COUNT; ++i) {
        uint64_t address = (uint64_t)(uintptr_t)(tx_buffers + i * BUFFER_SIZE);
        tx_ring[i].address_low = (uint32_t)address;
        tx_ring[i].address_high = (uint32_t)(address >> 32);
    }
    for (uint32_t i = 0; i < 6; ++i) {
        dev->mac[i] = inb((uint16_t)(io + REG_MAC + i));
    }

    outb((uint16_t)(io + REG_9346CR), 0xc0);        /* unlock config regs */
    outw((uint16_t)(io + REG_RMS), 0x1fff);
    outb((uint16_t)(io + REG_ETTHR), 0x3b);
    outl((uint16_t)(io + REG_TNPDS), (uint32_t)(uintptr_t)tx_ring);
    outl((uint16_t)(io + REG_TNPDS + 4), 0);
    outl((uint16_t)(io + REG_RDSAR), (uint32_t)(uintptr_t)rx_ring);
    outl((uint16_t)(io + REG_RDSAR + 4), 0);
    tx_ring[TX_COUNT - 1].opts1 = DESC_EOR;
    outb((uint16_t)(io + REG_CR), 0x0c);            /* RX + TX enable */
    outl((uint16_t)(io + REG_TCR), 0x03000700);
    outl((uint16_t)(io + REG_RCR), 0x0000e70e);     /* broadcast+multicast+own */
    outw((uint16_t)(io + REG_IMR), 0);              /* polling only */
    outw((uint16_t)(io + REG_ISR), 0xffff);
    outb((uint16_t)(io + REG_9346CR), 0x00);        /* lock config regs */
    rx_next = 0;
    tx_next = 0;

    dev->send = rtl8169_send;
    dev->receive = rtl8169_receive;
    dev->link_up = rtl8169_link_up;
    return true;
}

const nic_driver_t nic_driver_rtl8169 = {
    .name = "rtl8169",
    .match = rtl8169_match,
    .init = rtl8169_init,
};

/*
 * Intel e1000 (82540/82543/82545 family) Ethernet driver.
 * Polling, legacy descriptors, one instance. Hardware access is MMIO via
 * BAR0 (identity mapped below 4 GiB).
 */
#include "perwix.h"
#include "netdev.h"

#define RX_COUNT 32
#define TX_COUNT 8
#define PAGE 4096

#define REG_CTRL 0x0000
#define REG_STATUS 0x0008
#define REG_IMC 0x00d8
#define REG_RCTL 0x0100
#define REG_TCTL 0x0400
#define REG_TIPG 0x0410
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH 0x2810
#define REG_RDT 0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH 0x3810
#define REG_TDT 0x3818
#define REG_RAL0 0x5400
#define REG_RAH0 0x5404

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t buffer;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} rx_desc_t;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t buffer;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} tx_desc_t;

static volatile uint32_t *regs;
static rx_desc_t *rx_ring;
static tx_desc_t *tx_ring;
static uint8_t *rx_buf[RX_COUNT];
static uint8_t *tx_buf[TX_COUNT];
static uint32_t rx_next;
static uint32_t tx_next;

static uint32_t rd(uint32_t offset) { return regs[offset / 4]; }
static void wr(uint32_t offset, uint32_t value) { regs[offset / 4] = value; }

/* Device IDs of the classic e1000 family (all share one register map). */
static bool e1000_match(uint16_t vendor, uint16_t device)
{
    static const uint16_t ids[] = {
        0x1000, 0x1001, 0x1004, 0x1008, 0x1009, 0x100c, 0x100d, 0x100e,
        0x100f, 0x1010, 0x1011, 0x1012, 0x1013, 0x1015, 0x1016, 0x1017,
        0x1018, 0x1019, 0x101a, 0x101d, 0x1026, 0x1027, 0x1028, 0x1075,
        0x1076, 0x1077, 0x1078, 0x1079, 0x107a, 0x107b, 0x107c,
    };
    if (vendor != 0x8086) {
        return false;
    }
    for (uint32_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (ids[i] == device) {
            return true;
        }
    }
    return false;
}

static bool e1000_send(netdev_t *dev, const uint8_t *frame, uint16_t length)
{
    (void)dev;
    tx_desc_t *d = &tx_ring[tx_next];
    for (uint32_t w = 0; (d->status & 1) == 0 && w < 1000000; ++w) {
        __asm__ volatile("pause");
    }
    if ((d->status & 1) == 0 || length > 2048) {
        return false;
    }
    uint8_t *to = tx_buf[tx_next];
    for (uint32_t i = 0; i < length; ++i) {
        to[i] = frame[i];
    }
    d->length = length;
    d->command = 0x0b;                      /* EOP | IFCS | RS */
    d->status = 0;
    uint32_t index = tx_next;
    tx_next = (tx_next + 1) % TX_COUNT;
    wr(REG_TDT, tx_next);
    for (uint32_t w = 0; (tx_ring[index].status & 1) == 0 && w < 1000000; ++w) {
        __asm__ volatile("pause");
    }
    return (tx_ring[index].status & 1) != 0;
}

static uint16_t e1000_receive(netdev_t *dev, uint8_t *buffer, uint16_t capacity)
{
    (void)dev;
    rx_desc_t *d = &rx_ring[rx_next];
    if ((d->status & 1) == 0) {
        return 0;
    }
    uint16_t length = 0;
    if (d->errors == 0 && d->length <= capacity) {
        length = d->length;
        for (uint16_t i = 0; i < length; ++i) {
            buffer[i] = rx_buf[rx_next][i];
        }
    }
    d->status = 0;
    wr(REG_RDT, rx_next);
    rx_next = (rx_next + 1) % RX_COUNT;
    /* A dropped/bad frame returns 0 as well; callers poll again. */
    return length;
}

static bool e1000_link_up(netdev_t *dev)
{
    (void)dev;
    return (rd(REG_STATUS) & 2U) != 0;
}

static bool e1000_init(uint8_t bus, uint8_t device, uint8_t function,
                       netdev_t *dev)
{
    uint32_t bar_low = pci_config_read32(bus, device, function, 0x10);
    if ((bar_low & 1) != 0) {
        return false;
    }
    uint64_t bar = bar_low & ~0x0fU;
    if (((bar_low >> 1) & 3) == 2) {
        bar |= (uint64_t)pci_config_read32(bus, device, function, 0x14) << 32;
    }
    if (bar == 0 || bar >= 0x100000000ULL) {
        return false;
    }
    nic_pci_enable(bus, device, function);
    regs = (volatile uint32_t *)(uintptr_t)bar;

    wr(REG_IMC, 0xffffffffU);
    wr(REG_CTRL, rd(REG_CTRL) | (1U << 26));        /* software reset */
    bool reset_done = false;
    for (uint32_t w = 0; w < 1000000; ++w) {
        if ((rd(REG_CTRL) & (1U << 26)) == 0) {
            reset_done = true;
            break;
        }
    }
    if (!reset_done) {
        return false;
    }

    rx_ring = nic_alloc_dma(1);
    tx_ring = nic_alloc_dma(1);
    if (rx_ring == NULL || tx_ring == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < RX_COUNT; ++i) {
        rx_buf[i] = nic_alloc_dma(1);
        if (rx_buf[i] == NULL) {
            return false;
        }
        rx_ring[i].buffer = (uint64_t)(uintptr_t)rx_buf[i];
    }
    for (uint32_t i = 0; i < TX_COUNT; ++i) {
        tx_buf[i] = nic_alloc_dma(1);
        if (tx_buf[i] == NULL) {
            return false;
        }
        tx_ring[i].buffer = (uint64_t)(uintptr_t)tx_buf[i];
        tx_ring[i].status = 1;                      /* descriptor done */
    }

    uint32_t ral = rd(REG_RAL0);
    uint32_t rah = rd(REG_RAH0);
    if ((rah & (1U << 31)) == 0) {
        return false;                               /* no valid MAC */
    }
    dev->mac[0] = (uint8_t)ral;
    dev->mac[1] = (uint8_t)(ral >> 8);
    dev->mac[2] = (uint8_t)(ral >> 16);
    dev->mac[3] = (uint8_t)(ral >> 24);
    dev->mac[4] = (uint8_t)rah;
    dev->mac[5] = (uint8_t)(rah >> 8);

    wr(REG_RDBAL, (uint32_t)(uintptr_t)rx_ring);
    wr(REG_RDBAH, (uint32_t)((uint64_t)(uintptr_t)rx_ring >> 32));
    wr(REG_RDLEN, RX_COUNT * sizeof(rx_desc_t));
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_COUNT - 1);
    wr(REG_TDBAL, (uint32_t)(uintptr_t)tx_ring);
    wr(REG_TDBAH, (uint32_t)((uint64_t)(uintptr_t)tx_ring >> 32));
    wr(REG_TDLEN, TX_COUNT * sizeof(tx_desc_t));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TIPG, 0x0060200a);
    wr(REG_TCTL, 0x0004010a);
    wr(REG_RCTL, 0x04008002);   /* enable, accept broadcast, strip CRC */
    rx_next = 0;
    tx_next = 0;

    dev->send = e1000_send;
    dev->receive = e1000_receive;
    dev->link_up = e1000_link_up;
    return true;
}

const nic_driver_t nic_driver_e1000 = {
    .name = "e1000",
    .match = e1000_match,
    .init = e1000_init,
};

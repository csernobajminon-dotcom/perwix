/*
 * virtio-net, legacy (transitional) PCI interface over port I/O.
 * Works with QEMU `-device virtio-net-pci,disable-modern=on` and with
 * default QEMU/KVM/VirtualBox/Hyper-V-style legacy virtio devices.
 * Queue 0 = receive, queue 1 = transmit.
 */
#include "perwix.h"
#include "netdev.h"

#define VIRTIO_HOST_FEATURES 0x00
#define VIRTIO_GUEST_FEATURES 0x04
#define VIRTIO_QUEUE_ADDRESS 0x08
#define VIRTIO_QUEUE_SIZE 0x0c
#define VIRTIO_QUEUE_SELECT 0x0e
#define VIRTIO_QUEUE_NOTIFY 0x10
#define VIRTIO_STATUS 0x12
#define VIRTIO_CONFIG 0x14

#define STATUS_ACK 1
#define STATUS_DRIVER 2
#define STATUS_DRIVER_OK 4
#define FEATURE_MAC (1U << 5)

#define DESC_WRITE 2
#define HEADER_SIZE 10              /* virtio_net_hdr without mergeable bufs */
#define BUFFER_SIZE 2048
#define RX_BUFFERS 16

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} vq_desc_t;

typedef struct {
    volatile vq_desc_t *desc;
    volatile uint16_t *avail;       /* flags, idx, ring[] */
    volatile uint8_t *used;         /* flags(2), idx(2), ring[]{id,len} */
    uint16_t size;
    uint16_t last_used;
    uint16_t avail_index;
} virtqueue_t;

static uint16_t io;
static virtqueue_t rx_queue;
static virtqueue_t tx_queue;
static uint8_t *rx_buffers;         /* RX_BUFFERS * BUFFER_SIZE */
static uint8_t *tx_buffer;

static bool virtio_match(uint16_t vendor, uint16_t device)
{
    /* 0x1000 = transitional network device (legacy interface present). */
    return vendor == 0x1af4 && device == 0x1000;
}

static bool queue_setup(virtqueue_t *queue, uint16_t index)
{
    outw((uint16_t)(io + VIRTIO_QUEUE_SELECT), index);
    uint32_t size = inw((uint16_t)(io + VIRTIO_QUEUE_SIZE));
    if (size == 0 || size > 1024) {
        return false;
    }
    uint32_t desc_bytes = 16 * size;
    uint32_t avail_bytes = 6 + 2 * size;
    uint32_t used_offset = (desc_bytes + avail_bytes + 4095U) & ~4095U;
    uint32_t total = used_offset + 6 + 8 * size;
    uint8_t *memory = nic_alloc_dma((total + 4095U) / 4096U);
    if (memory == NULL) {
        return false;
    }
    queue->desc = (volatile vq_desc_t *)memory;
    queue->avail = (volatile uint16_t *)(memory + desc_bytes);
    queue->used = memory + used_offset;
    queue->size = (uint16_t)size;
    queue->last_used = 0;
    queue->avail_index = 0;
    outl((uint16_t)(io + VIRTIO_QUEUE_ADDRESS),
         (uint32_t)((uintptr_t)memory >> 12));
    return true;
}

static void post_rx(uint16_t slot)
{
    rx_queue.desc[slot].address = (uint64_t)(uintptr_t)(rx_buffers + slot * BUFFER_SIZE);
    rx_queue.desc[slot].length = BUFFER_SIZE;
    rx_queue.desc[slot].flags = DESC_WRITE;
    rx_queue.desc[slot].next = 0;
    rx_queue.avail[2 + (rx_queue.avail_index % rx_queue.size)] = slot;
    __asm__ volatile("" ::: "memory");
    rx_queue.avail[1] = ++rx_queue.avail_index;
}

static bool virtio_send(netdev_t *dev, const uint8_t *frame, uint16_t length)
{
    (void)dev;
    if (length + HEADER_SIZE > BUFFER_SIZE) {
        return false;
    }
    for (uint32_t i = 0; i < HEADER_SIZE; ++i) {
        tx_buffer[i] = 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
        tx_buffer[HEADER_SIZE + i] = frame[i];
    }
    tx_queue.desc[0].address = (uint64_t)(uintptr_t)tx_buffer;
    tx_queue.desc[0].length = (uint32_t)length + HEADER_SIZE;
    tx_queue.desc[0].flags = 0;
    tx_queue.desc[0].next = 0;
    tx_queue.avail[2 + (tx_queue.avail_index % tx_queue.size)] = 0;
    __asm__ volatile("" ::: "memory");
    tx_queue.avail[1] = ++tx_queue.avail_index;
    outw((uint16_t)(io + VIRTIO_QUEUE_NOTIFY), 1);

    for (uint32_t w = 0; w < 1000000; ++w) {
        uint16_t used_index = *(volatile uint16_t *)(tx_queue.used + 2);
        if (used_index != tx_queue.last_used) {
            tx_queue.last_used = used_index;
            return true;
        }
    }
    return false;
}

static uint16_t virtio_receive(netdev_t *dev, uint8_t *buffer, uint16_t capacity)
{
    (void)dev;
    uint16_t used_index = *(volatile uint16_t *)(rx_queue.used + 2);
    if (used_index == rx_queue.last_used) {
        return 0;
    }
    __asm__ volatile("" ::: "memory");
    volatile uint32_t *entry = (volatile uint32_t *)(rx_queue.used + 4 +
        8U * (rx_queue.last_used % rx_queue.size));
    uint16_t slot = (uint16_t)entry[0];
    uint32_t total = entry[1];
    ++rx_queue.last_used;

    uint16_t length = 0;
    if (slot < RX_BUFFERS && total > HEADER_SIZE &&
        total - HEADER_SIZE <= capacity) {
        length = (uint16_t)(total - HEADER_SIZE);
        const uint8_t *from = rx_buffers + slot * BUFFER_SIZE + HEADER_SIZE;
        for (uint16_t i = 0; i < length; ++i) {
            buffer[i] = from[i];
        }
    }
    if (slot < RX_BUFFERS) {
        post_rx(slot);
        outw((uint16_t)(io + VIRTIO_QUEUE_NOTIFY), 0);
    }
    return length;
}

static bool virtio_link_up(netdev_t *dev)
{
    (void)dev;
    return true;
}

static bool virtio_init(uint8_t bus, uint8_t device, uint8_t function,
                        netdev_t *dev)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x10);
    if ((bar & 1U) == 0) {
        return false;                       /* needs the legacy I/O BAR */
    }
    io = (uint16_t)(bar & ~3U);
    nic_pci_enable(bus, device, function);

    outb((uint16_t)(io + VIRTIO_STATUS), 0);            /* reset */
    outb((uint16_t)(io + VIRTIO_STATUS), STATUS_ACK);
    outb((uint16_t)(io + VIRTIO_STATUS), STATUS_ACK | STATUS_DRIVER);
    uint32_t features = inl((uint16_t)(io + VIRTIO_HOST_FEATURES));
    if ((features & FEATURE_MAC) == 0) {
        return false;
    }
    outl((uint16_t)(io + VIRTIO_GUEST_FEATURES), FEATURE_MAC);

    if (!queue_setup(&rx_queue, 0) || !queue_setup(&tx_queue, 1)) {
        return false;
    }
    rx_buffers = nic_alloc_dma(RX_BUFFERS * BUFFER_SIZE / 4096);
    tx_buffer = nic_alloc_dma(1);
    if (rx_buffers == NULL || tx_buffer == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < 6; ++i) {
        dev->mac[i] = inb((uint16_t)(io + VIRTIO_CONFIG + i));
    }
    for (uint16_t i = 0; i < RX_BUFFERS; ++i) {
        post_rx(i);
    }
    outb((uint16_t)(io + VIRTIO_STATUS),
         STATUS_ACK | STATUS_DRIVER | STATUS_DRIVER_OK);
    outw((uint16_t)(io + VIRTIO_QUEUE_NOTIFY), 0);

    dev->send = virtio_send;
    dev->receive = virtio_receive;
    dev->link_up = virtio_link_up;
    return true;
}

const nic_driver_t nic_driver_virtio = {
    .name = "virtio-net",
    .match = virtio_match,
    .init = virtio_init,
};

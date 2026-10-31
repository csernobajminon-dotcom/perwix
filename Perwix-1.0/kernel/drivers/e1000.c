#include "perwix.h"

#define E1000_RX_COUNT 32
#define E1000_TX_COUNT 8
#define E1000_FRAME_MAX 2048
#define E1000_PAGE_SIZE 4096

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
} rx_descriptor_t;

typedef struct __attribute__((packed, aligned(16))) {
    uint64_t buffer;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} tx_descriptor_t;

static volatile uint32_t *registers;
static rx_descriptor_t *rx_ring;
static tx_descriptor_t *tx_ring;
static uint8_t *rx_buffers[E1000_RX_COUNT];
static uint8_t *tx_buffers[E1000_TX_COUNT];
static uint8_t mac_address[6];
static uint32_t rx_next;
static uint32_t tx_next;
static bool initialized;
static bool arp_gateway_ready;
static uint8_t gateway_mac[6];
static uint16_t ping_sequence;
static bool ping_reply_received;
static uint32_t ping_target;
static uint32_t received_frames;

static const uint32_t local_ip = 0x0a00020f;
static const uint32_t gateway_ip = 0x0a000202;

static uint32_t reg_read(uint32_t offset)
{
    return registers[offset / 4];
}

static void reg_write(uint32_t offset, uint32_t value)
{
    registers[offset / 4] = value;
}

static void zero_memory(void *memory, uint32_t bytes)
{
    uint8_t *cursor = memory;
    for (uint32_t i = 0; i < bytes; ++i) {
        cursor[i] = 0;
    }
}

static void copy_memory(void *destination, const void *source, uint32_t bytes)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    for (uint32_t i = 0; i < bytes; ++i) {
        to[i] = from[i];
    }
}

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | data[3];
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t internet_checksum(const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) {
        sum += ((uint16_t)data[i] << 8) | data[i + 1];
    }
    if ((length & 1) != 0) {
        sum += (uint16_t)data[length - 1] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void send_arp_request(uint32_t target_ip);
static void receive_poll(void);

static bool send_frame(const uint8_t *frame, uint16_t length)
{
    if (!initialized || length > E1000_FRAME_MAX) {
        return false;
    }
    uint32_t index = tx_next;
    tx_descriptor_t *descriptor = &tx_ring[index];
    for (uint32_t wait = 0; (descriptor->status & 1) == 0 && wait < 1000000;
         ++wait) {
        __asm__ volatile("pause");
    }
    if ((descriptor->status & 1) == 0) {
        return false;
    }

    copy_memory(tx_buffers[index], frame, length);
    descriptor->length = length;
    descriptor->command = 0x0b;
    descriptor->status = 0;
    tx_next = (index + 1) % E1000_TX_COUNT;
    reg_write(REG_TDT, tx_next);
    for (uint32_t wait = 0; (descriptor->status & 1) == 0 && wait < 1000000;
         ++wait) {
        __asm__ volatile("pause");
    }
    return (descriptor->status & 1) != 0;
}

static bool ensure_gateway(void)
{
    if (arp_gateway_ready) {
        return true;
    }
    send_arp_request(gateway_ip);
    uint64_t start = system_ticks;
    while (!arp_gateway_ready && system_ticks - start < 150) {
        receive_poll();
        __asm__ volatile("pause");
    }
    return arp_gateway_ready;
}

static void send_arp_request(uint32_t target_ip)
{
    uint8_t frame[42] = {0};
    for (uint32_t i = 0; i < 6; ++i) {
        frame[i] = 0xff;
    }
    copy_memory(frame + 6, mac_address, 6);
    write_be16(frame + 12, 0x0806);
    write_be16(frame + 14, 1);
    write_be16(frame + 16, 0x0800);
    frame[18] = 6;
    frame[19] = 4;
    write_be16(frame + 20, 1);
    copy_memory(frame + 22, mac_address, 6);
    write_be32(frame + 28, local_ip);
    write_be32(frame + 38, target_ip);
    (void)send_frame(frame, sizeof(frame));
}

static void process_arp(const uint8_t *frame, uint16_t length)
{
    if (length < 42 || read_be16(frame + 20) != 2 ||
        read_be32(frame + 28) != gateway_ip) {
        return;
    }
    copy_memory(gateway_mac, frame + 22, 6);
    arp_gateway_ready = true;
}

static void process_ipv4(const uint8_t *frame, uint16_t length)
{
    if (length < 34 || (frame[14] >> 4) != 4 ||
        read_be32(frame + 30) != local_ip) {
        return;
    }
    uint32_t header_length = (frame[14] & 0x0f) * 4;
    if (header_length < 20 || length < 14 + header_length) {
        return;
    }
    switch (frame[23]) {
    case 1: {
        if (read_be32(frame + 26) != ping_target ||
            length < 14 + header_length + 8) {
            return;
        }
        const uint8_t *icmp = frame + 14 + header_length;
        if (icmp[0] == 0 && icmp[1] == 0 && read_be16(icmp + 4) == 0xbeef &&
            read_be16(icmp + 6) == ping_sequence) {
            ping_reply_received = true;
        }
        break;
    }
    default:
        break;
    }
}

static void process_frame(const uint8_t *frame, uint16_t length)
{
    if (length < 14) {
        return;
    }
    switch (read_be16(frame + 12)) {
    case 0x0806:
        process_arp(frame, length);
        break;
    case 0x0800:
        process_ipv4(frame, length);
        break;
    default:
        break;
    }
}

static void receive_poll(void)
{
    if (!initialized) {
        return;
    }
    for (;;) {
        rx_descriptor_t *descriptor = &rx_ring[rx_next];
        if ((descriptor->status & 1) == 0) {
            break;
        }
        uint16_t length = descriptor->length;
        if (descriptor->errors == 0 && length >= 4) {
            ++received_frames;
            process_frame(rx_buffers[rx_next], (uint16_t)(length - 4));
        }
        descriptor->status = 0;
        reg_write(REG_RDT, rx_next);
        rx_next = (rx_next + 1) % E1000_RX_COUNT;
    }
}

static bool allocate_rings(void)
{
    rx_ring = pmm_alloc_page();
    tx_ring = pmm_alloc_page();
    if (rx_ring == NULL || tx_ring == NULL) {
        return false;
    }
    zero_memory(rx_ring, E1000_PAGE_SIZE);
    zero_memory(tx_ring, E1000_PAGE_SIZE);

    for (uint32_t i = 0; i < E1000_RX_COUNT; ++i) {
        rx_buffers[i] = pmm_alloc_page();
        if (rx_buffers[i] == NULL) {
            return false;
        }
        zero_memory(rx_buffers[i], E1000_PAGE_SIZE);
        rx_ring[i].buffer = (uint64_t)(uintptr_t)rx_buffers[i];
    }
    for (uint32_t i = 0; i < E1000_TX_COUNT; ++i) {
        tx_buffers[i] = pmm_alloc_page();
        if (tx_buffers[i] == NULL) {
            return false;
        }
        zero_memory(tx_buffers[i], E1000_PAGE_SIZE);
        tx_ring[i].buffer = (uint64_t)(uintptr_t)tx_buffers[i];
        tx_ring[i].status = 1;
    }
    return true;
}

bool net_init(void)
{
    initialized = false;
    for (uint32_t i = 0; i < pci_device_count(); ++i) {
        uint8_t bus, device, function, class_code, subclass, interface;
        uint16_t vendor_id, device_id;
        if (!pci_device_get(i, &bus, &device, &function, &vendor_id,
                            &device_id, &class_code, &subclass, &interface)) {
            continue;
        }
        (void)interface;
        if (vendor_id != 0x8086 || device_id != 0x100e ||
            class_code != 0x02 || subclass != 0x00) {
            continue;
        }

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

        uint32_t command = pci_config_read32(bus, device, function, 0x04);
        pci_config_write32(bus, device, function, 0x04,
                           (command & 0xffff0000U) | ((command & 0xffffU) | 0x0006U));
        registers = (volatile uint32_t *)(uintptr_t)bar;
        reg_write(REG_IMC, 0xffffffffU);
        reg_write(REG_CTRL, reg_read(REG_CTRL) | (1U << 26));
        bool reset_complete = false;
        for (uint32_t wait = 0; wait < 1000000; ++wait) {
            if ((reg_read(REG_CTRL) & (1U << 26)) == 0) {
                reset_complete = true;
                break;
            }
        }
        if (!reset_complete || !allocate_rings()) {
            registers = NULL;
            return false;
        }

        uint32_t ral = reg_read(REG_RAL0);
        uint32_t rah = reg_read(REG_RAH0);
        mac_address[0] = (uint8_t)ral;
        mac_address[1] = (uint8_t)(ral >> 8);
        mac_address[2] = (uint8_t)(ral >> 16);
        mac_address[3] = (uint8_t)(ral >> 24);
        mac_address[4] = (uint8_t)rah;
        mac_address[5] = (uint8_t)(rah >> 8);
        if ((rah & (1U << 31)) == 0) {
            registers = NULL;
            return false;
        }

        reg_write(REG_RDBAL, (uint32_t)(uintptr_t)rx_ring);
        reg_write(REG_RDBAH, (uint32_t)((uint64_t)(uintptr_t)rx_ring >> 32));
        reg_write(REG_RDLEN, E1000_RX_COUNT * sizeof(rx_descriptor_t));
        reg_write(REG_RDH, 0);
        reg_write(REG_RDT, E1000_RX_COUNT - 1);
        reg_write(REG_TDBAL, (uint32_t)(uintptr_t)tx_ring);
        reg_write(REG_TDBAH, (uint32_t)((uint64_t)(uintptr_t)tx_ring >> 32));
        reg_write(REG_TDLEN, E1000_TX_COUNT * sizeof(tx_descriptor_t));
        reg_write(REG_TDH, 0);
        reg_write(REG_TDT, 0);
        reg_write(REG_TIPG, 0x0060200a);
        reg_write(REG_TCTL, 0x0004010a);
        reg_write(REG_RCTL, 0x04008002);
        rx_next = 0;
        tx_next = 0;
        arp_gateway_ready = false;
        initialized = true;
        return true;
    }
    return false;
}

bool net_ready(void)
{
    return initialized;
}

uint32_t net_received_frames(void)
{
    return received_frames;
}

bool net_ping(uint32_t ipv4_address)
{
    if (!initialized || ipv4_address == 0) {
        return false;
    }

    if (!ensure_gateway()) {
        return false;
    }

    uint8_t frame[42] = {0};
    copy_memory(frame, gateway_mac, 6);
    copy_memory(frame + 6, mac_address, 6);
    write_be16(frame + 12, 0x0800);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    write_be16(ip + 2, 28);
    write_be16(ip + 4, ++ping_sequence);
    ip[8] = 64;
    ip[9] = 1;
    write_be32(ip + 12, local_ip);
    write_be32(ip + 16, ipv4_address);
    write_be16(ip + 10, internet_checksum(ip, 20));

    uint8_t *icmp = ip + 20;
    icmp[0] = 8;
    write_be16(icmp + 4, 0xbeef);
    write_be16(icmp + 6, ping_sequence);
    write_be16(icmp + 2, internet_checksum(icmp, 8));

    ping_target = ipv4_address;
    ping_reply_received = false;
    if (!send_frame(frame, sizeof(frame))) {
        return false;
    }

    uint64_t start = system_ticks;
    while (!ping_reply_received && system_ticks - start < 300) {
        receive_poll();
        __asm__ volatile("pause");
    }
    return ping_reply_received;
}


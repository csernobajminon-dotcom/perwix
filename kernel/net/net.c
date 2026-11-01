/*
 * Perwix network stack (polling, single interface).
 *
 * Layers: Ethernet -> ARP / IPv4 -> ICMP / UDP / TCP
 * Clients: DHCP, DNS (A records), HTTP/1.0 GET over a minimal TCP client.
 *
 * NIC drivers plug in through netdev_t (see netdev.h). Everything here is
 * synchronous: functions that wait for the network poll the NIC while
 * `system_ticks` (100 Hz PIT) advances, so interrupts must be enabled.
 */
#include "perwix.h"
#include "netdev.h"
#include "driver.h"

#define ETH_HDR 14
#define IP_HDR 20
#define ARP_CACHE_SIZE 16
#define DNS_CACHE_SIZE 8
#define NAME_MAX 64

#define PROTO_ICMP 1
#define PROTO_TCP 6
#define PROTO_UDP 17

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define DNS_LOCAL_PORT 53053
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static netdev_t nic;
static bool have_nic;
static bool configured;
static bool dhcp_ok;

static uint32_t local_ip;
static uint32_t netmask;
static uint32_t gateway;
static uint32_t dns_server;

static uint32_t rx_frames;
static uint32_t tx_frames;
static uint16_t ip_id;

static uint8_t rx_buf[NETDEV_FRAME_MAX];
static uint8_t tx_buf[NETDEV_FRAME_MAX];
static uint8_t seg_buf[NETDEV_FRAME_MAX];   /* TCP/UDP payload staging */

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    bool used;
} arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t arp_next_slot;

typedef struct {
    char name[NAME_MAX];
    uint32_t address;
    bool used;
} dns_entry_t;
static dns_entry_t dns_cache[DNS_CACHE_SIZE];
static uint32_t dns_next_slot;

/* Driver registry entries (names only; discovery is done by net_init). */
static perwix_driver_t registry_entries[6];

static const nic_driver_t *const nic_drivers[] = {
    &nic_driver_e1000, &nic_driver_rtl8139, &nic_driver_rtl8169,
    &nic_driver_virtio, &nic_driver_pcnet, &nic_driver_ne2k,
};
#define NIC_DRIVER_COUNT (sizeof(nic_drivers) / sizeof(nic_drivers[0]))

/* ------------------------------------------------------------------ */
/* Small helpers (no libc in the kernel)                               */
/* ------------------------------------------------------------------ */

static void mcopy(void *to, const void *from, uint32_t bytes)
{
    uint8_t *d = to;
    const uint8_t *s = from;
    for (uint32_t i = 0; i < bytes; ++i) {
        d[i] = s[i];
    }
}

static void mzero(void *memory, uint32_t bytes)
{
    uint8_t *d = memory;
    for (uint32_t i = 0; i < bytes; ++i) {
        d[i] = 0;
    }
}

static bool mequal(const void *a, const void *b, uint32_t bytes)
{
    const uint8_t *x = a;
    const uint8_t *y = b;
    for (uint32_t i = 0; i < bytes; ++i) {
        if (x[i] != y[i]) {
            return false;
        }
    }
    return true;
}

static uint32_t text_length(const char *text)
{
    uint32_t n = 0;
    while (text[n] != '\0') {
        ++n;
    }
    return n;
}

static bool text_same(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | p[3];
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Internet checksum helpers: accumulate, then fold. */
static uint32_t csum_add(uint32_t sum, const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i + 1 < length; i += 2) {
        sum += ((uint32_t)data[i] << 8) | data[i + 1];
    }
    if ((length & 1U) != 0) {
        sum += (uint32_t)data[length - 1] << 8;
    }
    return sum;
}

static uint16_t csum_fold(uint32_t sum)
{
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* Wait helper: poll the NIC until predicate-style loops time out. */
static bool timed_out(uint64_t start, uint64_t ticks)
{
    return system_ticks - start >= ticks;
}

/* ------------------------------------------------------------------ */
/* Text <-> address conversion (shared with the shell)                 */
/* ------------------------------------------------------------------ */

bool parse_ipv4_text(const char *text, uint32_t *address)
{
    uint32_t octets[4] = {0, 0, 0, 0};
    for (uint32_t part = 0; part < 4; ++part) {
        uint32_t digits = 0;
        while (*text >= '0' && *text <= '9') {
            octets[part] = octets[part] * 10 + (uint32_t)(*text - '0');
            if (octets[part] > 255 || ++digits > 3) {
                return false;
            }
            ++text;
        }
        if (digits == 0 || (part < 3 && *text++ != '.') ||
            (part == 3 && *text != '\0')) {
            return false;
        }
    }
    *address = (octets[0] << 24) | (octets[1] << 16) |
        (octets[2] << 8) | octets[3];
    return true;
}

void console_write_ipv4(uint32_t address)
{
    console_write_u64((address >> 24) & 0xff);
    console_write(".");
    console_write_u64((address >> 16) & 0xff);
    console_write(".");
    console_write_u64((address >> 8) & 0xff);
    console_write(".");
    console_write_u64(address & 0xff);
}

void console_write_mac(const uint8_t mac[6])
{
    static const char digits[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 6; ++i) {
        char text[3] = { digits[mac[i] >> 4], digits[mac[i] & 15], '\0' };
        console_write(text);
        if (i != 5) {
            console_write(":");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Ethernet + ARP                                                      */
/* ------------------------------------------------------------------ */

static bool eth_send(const uint8_t *frame, uint32_t length)
{
    if (!have_nic || length > NETDEV_FRAME_MAX) {
        return false;
    }
    if (length < 60) {
        /* Pad to the Ethernet minimum (tx_buf tail is already zeroed
         * by the callers that use tx_buf; copy for others). */
        static uint8_t padded[64];
        mzero(padded, sizeof(padded));
        mcopy(padded, frame, length);
        ++tx_frames;
        return nic.send(&nic, padded, 60);
    }
    ++tx_frames;
    return nic.send(&nic, frame, (uint16_t)length);
}

static void arp_put(uint32_t ip, const uint8_t *mac)
{
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].used && arp_cache[i].ip == ip) {
            mcopy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    arp_entry_t *slot = &arp_cache[arp_next_slot];
    arp_next_slot = (arp_next_slot + 1) % ARP_CACHE_SIZE;
    slot->used = true;
    slot->ip = ip;
    mcopy(slot->mac, mac, 6);
}

static bool arp_lookup(uint32_t ip, uint8_t *mac)
{
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].used && arp_cache[i].ip == ip) {
            mcopy(mac, arp_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

static void arp_send(uint16_t operation, const uint8_t *target_mac,
                     uint32_t target_ip)
{
    uint8_t frame[42];
    mzero(frame, sizeof(frame));
    if (operation == 1) {
        for (uint32_t i = 0; i < 6; ++i) {
            frame[i] = 0xff;
        }
    } else {
        mcopy(frame, target_mac, 6);
    }
    mcopy(frame + 6, nic.mac, 6);
    wr16(frame + 12, 0x0806);
    wr16(frame + 14, 1);
    wr16(frame + 16, 0x0800);
    frame[18] = 6;
    frame[19] = 4;
    wr16(frame + 20, operation);
    mcopy(frame + 22, nic.mac, 6);
    wr32(frame + 28, local_ip);
    if (operation == 2) {
        mcopy(frame + 32, target_mac, 6);
    }
    wr32(frame + 38, target_ip);
    (void)eth_send(frame, sizeof(frame));
}

static void arp_input(const uint8_t *frame, uint32_t length)
{
    if (length < 42 || rd16(frame + 14) != 1 || rd16(frame + 16) != 0x0800) {
        return;
    }
    uint16_t operation = rd16(frame + 20);
    uint32_t sender_ip = rd32(frame + 28);
    arp_put(sender_ip, frame + 22);
    if (operation == 1 && local_ip != 0 && rd32(frame + 38) == local_ip) {
        arp_send(2, frame + 22, sender_ip);
    }
}

/* Resolve the MAC for a next-hop address, with retries. */
static bool arp_resolve(uint32_t ip, uint8_t *mac)
{
    if (arp_lookup(ip, mac)) {
        return true;
    }
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        arp_send(1, NULL, ip);
        uint64_t start = system_ticks;
        while (!timed_out(start, 50)) {
            net_poll();
            if (arp_lookup(ip, mac)) {
                return true;
            }
            __asm__ volatile("pause");
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* IPv4                                                                */
/* ------------------------------------------------------------------ */

static bool ip_send_raw(const uint8_t *dst_mac, uint32_t src_ip,
                        uint32_t dst_ip, uint8_t protocol,
                        const uint8_t *payload, uint32_t length)
{
    if (ETH_HDR + IP_HDR + length > NETDEV_FRAME_MAX) {
        return false;
    }
    mzero(tx_buf, ETH_HDR + IP_HDR);
    mcopy(tx_buf, dst_mac, 6);
    mcopy(tx_buf + 6, nic.mac, 6);
    wr16(tx_buf + 12, 0x0800);
    uint8_t *ip = tx_buf + ETH_HDR;
    ip[0] = 0x45;
    wr16(ip + 2, (uint16_t)(IP_HDR + length));
    wr16(ip + 4, ++ip_id);
    ip[8] = 64;
    ip[9] = protocol;
    wr32(ip + 12, src_ip);
    wr32(ip + 16, dst_ip);
    wr16(ip + 10, csum_fold(csum_add(0, ip, IP_HDR)));
    mcopy(ip + IP_HDR, payload, length);
    return eth_send(tx_buf, ETH_HDR + IP_HDR + length);
}

static bool ip_send(uint32_t dst_ip, uint8_t protocol,
                    const uint8_t *payload, uint32_t length)
{
    uint8_t mac[6];
    if (!have_nic) {
        return false;
    }
    if (dst_ip == 0xffffffffU) {
        for (uint32_t i = 0; i < 6; ++i) {
            mac[i] = 0xff;
        }
    } else {
        uint32_t hop = ((dst_ip ^ local_ip) & netmask) == 0 ? dst_ip : gateway;
        /* Resolve first: waiting may poll the NIC and use tx_buf. */
        if (!arp_resolve(hop, mac)) {
            return false;
        }
    }
    return ip_send_raw(mac, local_ip, dst_ip, protocol, payload, length);
}

/* Resolve the next hop up front. Callers that stage data in a shared
 * buffer call this first, because waiting for ARP polls the NIC and the
 * receive path may reuse those buffers. */
static bool route_ready(uint32_t dst_ip)
{
    uint8_t mac[6];
    if (!have_nic) {
        return false;
    }
    if (dst_ip == 0xffffffffU) {
        return true;
    }
    uint32_t hop = ((dst_ip ^ local_ip) & netmask) == 0 ? dst_ip : gateway;
    return arp_resolve(hop, mac);
}

/* ------------------------------------------------------------------ */
/* ICMP (ping + echo replies)                                          */
/* ------------------------------------------------------------------ */

static uint32_t ping_target;
static uint16_t ping_sequence;
static volatile bool ping_reply_received;

static void icmp_input(const uint8_t *frame, uint32_t src_ip,
                       const uint8_t *icmp, uint32_t length)
{
    if (length < 8) {
        return;
    }
    if (icmp[0] == 0 && src_ip == ping_target &&
        rd16(icmp + 4) == 0xbeef && rd16(icmp + 6) == ping_sequence) {
        ping_reply_received = true;
    } else if (icmp[0] == 8 && length <= 1400 && local_ip != 0) {
        /* Answer echo requests so others can ping us. Own buffer: this
         * runs from the receive path while other code may use seg_buf. */
        static uint8_t reply[1400];
        mcopy(reply, icmp, length);
        reply[0] = 0;
        wr16(reply + 2, 0);
        wr16(reply + 2, csum_fold(csum_add(0, reply, length)));
        (void)ip_send_raw(frame + 6, local_ip, src_ip, PROTO_ICMP,
                          reply, length);
    }
}

bool net_ping_time(uint32_t address, uint32_t *rtt_ms)
{
    if (!have_nic || address == 0 || !net_configure()) {
        return false;
    }
    if (!route_ready(address)) {
        return false;
    }
    static uint8_t echo[40];
    mzero(echo, sizeof(echo));
    echo[0] = 8;
    wr16(echo + 4, 0xbeef);
    wr16(echo + 6, ++ping_sequence);
    for (uint32_t i = 8; i < sizeof(echo); ++i) {
        echo[i] = (uint8_t)('a' + (i % 23));
    }
    wr16(echo + 2, csum_fold(csum_add(0, echo, sizeof(echo))));

    ping_target = address;
    ping_reply_received = false;
    uint64_t start = system_ticks;
    if (!ip_send(address, PROTO_ICMP, echo, sizeof(echo))) {
        return false;
    }
    while (!ping_reply_received && !timed_out(start, 300)) {
        net_poll();
        __asm__ volatile("pause");
    }
    if (ping_reply_received && rtt_ms != NULL) {
        *rtt_ms = (uint32_t)(system_ticks - start) * (1000 / PERWIX_TIMER_HZ);
    }
    return ping_reply_received;
}

bool net_ping(uint32_t address)
{
    return net_ping_time(address, NULL);
}

/* ------------------------------------------------------------------ */
/* UDP                                                                 */
/* ------------------------------------------------------------------ */

static void dhcp_input(const uint8_t *data, uint32_t length);
static void dns_input(const uint8_t *data, uint32_t length);

/* Build a UDP datagram in seg_buf (checksum 0 = unused, legal in IPv4). */
static uint32_t udp_build(uint16_t sport, uint16_t dport,
                          const uint8_t *data, uint32_t length)
{
    wr16(seg_buf, sport);
    wr16(seg_buf + 2, dport);
    wr16(seg_buf + 4, (uint16_t)(8 + length));
    wr16(seg_buf + 6, 0);
    mcopy(seg_buf + 8, data, length);
    return 8 + length;
}

static bool udp_send(uint32_t dst_ip, uint16_t sport, uint16_t dport,
                     const uint8_t *data, uint32_t length)
{
    if (length > 1400 || !route_ready(dst_ip)) {
        return false;
    }
    return ip_send(dst_ip, PROTO_UDP, seg_buf, udp_build(sport, dport, data, length));
}

static void udp_input(const uint8_t *udp, uint32_t length)
{
    if (length < 8) {
        return;
    }
    uint16_t sport = rd16(udp);
    uint16_t dport = rd16(udp + 2);
    uint32_t udp_length = rd16(udp + 4);
    if (udp_length < 8 || udp_length > length) {
        return;
    }
    if (dport == DHCP_CLIENT_PORT && sport == DHCP_SERVER_PORT) {
        dhcp_input(udp + 8, udp_length - 8);
    } else if (dport == DNS_LOCAL_PORT && sport == 53) {
        dns_input(udp + 8, udp_length - 8);
    }
}

/* ------------------------------------------------------------------ */
/* DHCP client                                                         */
/* ------------------------------------------------------------------ */

#define DHCP_XID 0x50575831U   /* "PWX1" */

static uint8_t dhcp_stage;          /* 0 idle, 1 discover sent, 2 request sent */
static uint32_t dhcp_offer_ip;
static uint32_t dhcp_server_ip;
static uint32_t dhcp_mask;
static uint32_t dhcp_router;
static uint32_t dhcp_dns;
static bool dhcp_got_offer;
static bool dhcp_got_ack;

static void dhcp_input(const uint8_t *data, uint32_t length)
{
    if (length < 240 || data[0] != 2 || rd32(data + 4) != DHCP_XID ||
        rd32(data + 236) != 0x63825363U ||
        !mequal(data + 28, nic.mac, 6)) {
        return;
    }
    uint8_t type = 0;
    uint32_t mask = 0, router = 0, dns = 0, server = 0;
    uint32_t i = 240;
    while (i < length && data[i] != 255) {
        if (data[i] == 0) {
            ++i;
            continue;
        }
        if (i + 1 >= length || i + 2 + data[i + 1] > length) {
            break;
        }
        uint8_t code = data[i];
        uint8_t size = data[i + 1];
        const uint8_t *value = data + i + 2;
        if (code == 53 && size == 1) {
            type = value[0];
        } else if (code == 1 && size == 4) {
            mask = rd32(value);
        } else if (code == 3 && size >= 4) {
            router = rd32(value);
        } else if (code == 6 && size >= 4) {
            dns = rd32(value);
        } else if (code == 54 && size == 4) {
            server = rd32(value);
        }
        i += 2U + size;
    }
    if (type == 2 && dhcp_stage == 1) {
        dhcp_offer_ip = rd32(data + 16);
        dhcp_server_ip = server;
        dhcp_got_offer = true;
    } else if (type == 5 && dhcp_stage == 2) {
        dhcp_offer_ip = rd32(data + 16);
        dhcp_mask = mask != 0 ? mask : 0xffffff00U;
        dhcp_router = router;
        dhcp_dns = dns;
        dhcp_got_ack = true;
    }
}

static void dhcp_send(uint8_t message_type)
{
    static uint8_t packet[300];
    mzero(packet, sizeof(packet));
    packet[0] = 1;                          /* BOOTREQUEST */
    packet[1] = 1;                          /* Ethernet */
    packet[2] = 6;
    wr32(packet + 4, DHCP_XID);
    wr16(packet + 10, 0x8000);              /* ask for broadcast replies */
    mcopy(packet + 28, nic.mac, 6);
    wr32(packet + 236, 0x63825363U);        /* magic cookie */

    uint32_t o = 240;
    packet[o++] = 53; packet[o++] = 1; packet[o++] = message_type;
    packet[o++] = 55; packet[o++] = 3;      /* parameter request list */
    packet[o++] = 1; packet[o++] = 3; packet[o++] = 6;
    if (message_type == 3) {                /* REQUEST: pick the offer */
        packet[o++] = 50; packet[o++] = 4;
        wr32(packet + o, dhcp_offer_ip); o += 4;
        packet[o++] = 54; packet[o++] = 4;
        wr32(packet + o, dhcp_server_ip); o += 4;
    }
    packet[o++] = 255;

    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint32_t length = udp_build(DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, 300);
    (void)ip_send_raw(broadcast, 0, 0xffffffffU, PROTO_UDP, seg_buf, length);
}

bool net_dhcp(void)
{
    if (!have_nic) {
        return false;
    }
    uint32_t saved_ip = local_ip;
    local_ip = 0;                           /* accept any unicast target */
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        dhcp_stage = 1;
        dhcp_got_offer = false;
        dhcp_got_ack = false;
        dhcp_send(1);
        uint64_t start = system_ticks;
        while (!dhcp_got_offer && !timed_out(start, 200)) {
            net_poll();
            __asm__ volatile("pause");
        }
        if (!dhcp_got_offer) {
            continue;
        }
        dhcp_stage = 2;
        dhcp_send(3);
        start = system_ticks;
        while (!dhcp_got_ack && !timed_out(start, 200)) {
            net_poll();
            __asm__ volatile("pause");
        }
        if (dhcp_got_ack) {
            dhcp_stage = 0;
            local_ip = dhcp_offer_ip;
            netmask = dhcp_mask;
            gateway = dhcp_router;
            dns_server = dhcp_dns != 0 ? dhcp_dns : dhcp_router;
            dhcp_ok = true;
            configured = true;
            return true;
        }
    }
    dhcp_stage = 0;
    local_ip = saved_ip;
    return false;
}

bool net_configure(void)
{
    if (!have_nic) {
        return false;
    }
    if (configured) {
        return true;
    }
    if (net_dhcp()) {
        return true;
    }
    /* No DHCP server answered: use QEMU user-network defaults. */
    local_ip = 0x0a00020f;
    netmask = 0xffffff00U;
    gateway = 0x0a000202;
    dns_server = 0x0a000203;
    dhcp_ok = false;
    configured = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* DNS resolver                                                        */
/* ------------------------------------------------------------------ */

static uint16_t dns_query_id;
static volatile bool dns_answer_ready;
static uint32_t dns_answer;

static void dns_input(const uint8_t *data, uint32_t length)
{
    if (length < 12 || rd16(data) != dns_query_id ||
        (data[2] & 0x80) == 0 || (data[3] & 0x0f) != 0) {
        return;
    }
    uint32_t questions = rd16(data + 4);
    uint32_t answers = rd16(data + 6);
    uint32_t i = 12;

    /* Skip a (possibly compressed) name. */
    #define SKIP_NAME()                                                   \
        do {                                                              \
            while (i < length && data[i] != 0 && (data[i] & 0xc0) != 0xc0) \
                i += 1U + data[i];                                        \
            if (i >= length) return;                                      \
            i += (data[i] & 0xc0) == 0xc0 ? 2U : 1U;                      \
        } while (0)

    for (uint32_t q = 0; q < questions; ++q) {
        SKIP_NAME();
        i += 4;
    }
    for (uint32_t a = 0; a < answers; ++a) {
        SKIP_NAME();
        if (i + 10 > length) {
            return;
        }
        uint16_t type = rd16(data + i);
        uint16_t rdlength = rd16(data + i + 8);
        i += 10;
        if (i + rdlength > length) {
            return;
        }
        if (type == 1 && rdlength == 4) {
            dns_answer = rd32(data + i);
            dns_answer_ready = true;
            return;
        }
        i += rdlength;
    }
    #undef SKIP_NAME
}

static uint32_t dns_encode_name(uint8_t *out, const char *name)
{
    uint32_t o = 0;
    uint32_t label_start = o++;
    for (const char *c = name;; ++c) {
        if (*c == '.' || *c == '\0') {
            uint32_t size = o - label_start - 1;
            if (size == 0 || size > 63) {
                return 0;
            }
            out[label_start] = (uint8_t)size;
            if (*c == '\0') {
                break;
            }
            label_start = o++;
        } else {
            out[o++] = (uint8_t)*c;
        }
    }
    out[o++] = 0;
    return o;
}

bool net_resolve(const char *host, uint32_t *address)
{
    if (parse_ipv4_text(host, address)) {
        return true;
    }
    uint32_t name_length = text_length(host);
    if (name_length == 0 || name_length >= NAME_MAX || !net_configure()) {
        return false;
    }
    for (uint32_t i = 0; i < DNS_CACHE_SIZE; ++i) {
        if (dns_cache[i].used && text_same(dns_cache[i].name, host)) {
            *address = dns_cache[i].address;
            return true;
        }
    }

    static uint8_t query[NAME_MAX + 32];
    mzero(query, sizeof(query));
    dns_query_id = (uint16_t)(0x5000 + (system_ticks & 0xfff));
    wr16(query, dns_query_id);
    wr16(query + 2, 0x0100);                /* recursion desired */
    wr16(query + 4, 1);
    uint32_t name_bytes = dns_encode_name(query + 12, host);
    if (name_bytes == 0) {
        return false;
    }
    uint32_t length = 12 + name_bytes;
    wr16(query + length, 1);                /* type A */
    wr16(query + length + 2, 1);            /* class IN */
    length += 4;

    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        dns_answer_ready = false;
        if (!udp_send(dns_server, DNS_LOCAL_PORT, 53, query, length)) {
            continue;
        }
        uint64_t start = system_ticks;
        while (!dns_answer_ready && !timed_out(start, 300)) {
            net_poll();
            __asm__ volatile("pause");
        }
        if (dns_answer_ready) {
            *address = dns_answer;
            dns_entry_t *slot = &dns_cache[dns_next_slot];
            dns_next_slot = (dns_next_slot + 1) % DNS_CACHE_SIZE;
            slot->used = true;
            slot->address = dns_answer;
            mcopy(slot->name, host, name_length + 1);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* TCP (single outgoing connection) + HTTP                             */
/* ------------------------------------------------------------------ */

typedef enum { TCP_CLOSED, TCP_SYN_SENT, TCP_OPEN } tcp_state_t;

static struct {
    tcp_state_t state;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t rcv_nxt;
    bool fin_received;
    bool reset;
    uint32_t received;
    void (*sink)(const char *data, uint32_t length);
} tcp;

static uint16_t tcp_port_counter;

static void tcp_send(uint8_t flags, const uint8_t *data, uint32_t length)
{
    static uint8_t segment[NETDEV_FRAME_MAX];
    uint32_t header = (flags & TCP_SYN) != 0 ? 24 : 20;
    if (header + length > 1460 + 24 || !route_ready(tcp.remote_ip)) {
        return;
    }
    mzero(segment, header);
    wr16(segment, tcp.local_port);
    wr16(segment + 2, tcp.remote_port);
    wr32(segment + 4, tcp.snd_nxt);
    wr32(segment + 8, (flags & TCP_ACK) != 0 ? tcp.rcv_nxt : 0);
    segment[12] = (uint8_t)((header / 4) << 4);
    segment[13] = flags;
    wr16(segment + 14, 8192);               /* receive window */
    if ((flags & TCP_SYN) != 0) {
        segment[20] = 2; segment[21] = 4;   /* MSS option */
        wr16(segment + 22, 1460);
    }
    mcopy(segment + header, data, length);

    /* Checksum over pseudo header + segment. */
    uint8_t pseudo[12];
    wr32(pseudo, local_ip);
    wr32(pseudo + 4, tcp.remote_ip);
    pseudo[8] = 0;
    pseudo[9] = PROTO_TCP;
    wr16(pseudo + 10, (uint16_t)(header + length));
    uint32_t sum = csum_add(0, pseudo, sizeof(pseudo));
    sum = csum_add(sum, segment, header + length);
    wr16(segment + 16, csum_fold(sum));

    (void)ip_send(tcp.remote_ip, PROTO_TCP, segment, header + length);
}

static void tcp_input(uint32_t src_ip, const uint8_t *seg, uint32_t length)
{
    if (tcp.state == TCP_CLOSED || length < 20 || src_ip != tcp.remote_ip ||
        rd16(seg) != tcp.remote_port || rd16(seg + 2) != tcp.local_port) {
        return;
    }
    uint32_t header = (uint32_t)(seg[12] >> 4) * 4;
    uint8_t flags = seg[13];
    uint32_t seq = rd32(seg + 4);
    uint32_t ack = rd32(seg + 8);
    if (header < 20 || header > length) {
        return;
    }
    if ((flags & TCP_RST) != 0) {
        tcp.reset = true;
        tcp.state = TCP_CLOSED;
        return;
    }
    if (tcp.state == TCP_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
            ack == tcp.snd_nxt) {
            tcp.rcv_nxt = seq + 1;
            tcp.snd_una = ack;
            tcp.state = TCP_OPEN;
            tcp_send(TCP_ACK, NULL, 0);
        }
        return;
    }

    if ((flags & TCP_ACK) != 0 && (int32_t)(ack - tcp.snd_una) > 0 &&
        (int32_t)(tcp.snd_nxt - ack) >= 0) {
        tcp.snd_una = ack;
    }
    uint32_t payload = length - header;
    bool in_order = seq == tcp.rcv_nxt;
    if (payload > 0) {
        if (in_order) {
            if (tcp.sink != NULL) {
                tcp.sink((const char *)(seg + header), payload);
            }
            tcp.rcv_nxt += payload;
            tcp.received += payload;
        }
        tcp_send(TCP_ACK, NULL, 0);          /* ACK, or duplicate ACK */
    }
    if ((flags & TCP_FIN) != 0 && seq + payload == tcp.rcv_nxt) {
        tcp.rcv_nxt += 1;
        tcp.fin_received = true;
        tcp_send(TCP_ACK, NULL, 0);
    }
}

static bool tcp_connect(uint32_t address, uint16_t port)
{
    tcp.remote_ip = address;
    tcp.remote_port = port;
    tcp.local_port = (uint16_t)(49152 + (tcp_port_counter++ & 0x3fff));
    uint32_t isn = 0x1000U + (uint32_t)system_ticks * 64U;
    tcp.snd_nxt = isn + 1;                  /* the SYN uses one number */
    tcp.snd_una = isn;
    tcp.rcv_nxt = 0;
    tcp.fin_received = false;
    tcp.reset = false;
    tcp.received = 0;
    tcp.state = TCP_SYN_SENT;
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        tcp.snd_nxt = isn;                  /* SYN carries the ISN itself */
        tcp_send(TCP_SYN, NULL, 0);
        tcp.snd_nxt = isn + 1;
        uint64_t start = system_ticks;
        while (tcp.state == TCP_SYN_SENT && !timed_out(start, 200)) {
            net_poll();
            __asm__ volatile("pause");
        }
        if (tcp.state == TCP_OPEN) {
            return true;
        }
        if (tcp.state == TCP_CLOSED) {
            return false;                    /* refused (RST) */
        }
    }
    tcp.state = TCP_CLOSED;
    return false;
}

/* Returns bytes received, or a negative error: -1 DNS, -2 connect,
 * -3 request not delivered. */
int net_http_get(const char *host, const char *path,
                 void (*sink)(const char *data, uint32_t length))
{
    uint32_t address;
    if (!net_resolve(host, &address)) {
        return -1;
    }
    tcp.sink = sink;
    if (!tcp_connect(address, 80)) {
        return -2;
    }

    static uint8_t request[256];
    uint32_t length = 0;
    const char *parts[] = { "GET ", path, " HTTP/1.0\r\nHost: ", host,
                            "\r\nUser-Agent: Perwix/1.1\r\n"
                            "Connection: close\r\n\r\n" };
    for (uint32_t p = 0; p < 5; ++p) {
        for (const char *c = parts[p]; *c != '\0' && length < sizeof(request); ++c) {
            request[length++] = (uint8_t)*c;
        }
    }

    uint32_t request_start = tcp.snd_nxt;
    tcp_send(TCP_PSH | TCP_ACK, request, length);
    tcp.snd_nxt += length;
    uint64_t last_send = system_ticks;
    uint64_t last_progress = system_ticks;
    uint32_t last_received = 0;
    uint32_t resends = 0;

    while (tcp.state == TCP_OPEN && !tcp.fin_received &&
           !timed_out(last_progress, 800)) {
        net_poll();
        if (tcp.received != last_received) {
            last_received = tcp.received;
            last_progress = system_ticks;
        }
        /* Retransmit the request if the server never acknowledged it. */
        if (tcp.snd_una == request_start && resends < 3 &&
            timed_out(last_send, 100)) {
            uint32_t saved = tcp.snd_nxt;
            tcp.snd_nxt = request_start;
            tcp_send(TCP_PSH | TCP_ACK, request, length);
            tcp.snd_nxt = saved;
            last_send = system_ticks;
            ++resends;
        }
        __asm__ volatile("pause");
    }

    bool delivered = tcp.snd_una != request_start || tcp.received != 0;
    if (tcp.state == TCP_OPEN) {
        tcp_send(TCP_FIN | TCP_ACK, NULL, 0);
        tcp.snd_nxt += 1;
        uint64_t start = system_ticks;
        while (!timed_out(start, 20)) {      /* brief wait for the last ACK */
            net_poll();
        }
    }
    tcp.state = TCP_CLOSED;
    tcp.sink = NULL;
    if (!delivered) {
        return -3;
    }
    return (int)tcp.received;
}

/* ------------------------------------------------------------------ */
/* Receive path                                                        */
/* ------------------------------------------------------------------ */

static void ipv4_input(const uint8_t *frame, uint32_t length)
{
    if (length < ETH_HDR + IP_HDR || (frame[14] >> 4) != 4) {
        return;
    }
    uint32_t header = (uint32_t)(frame[14] & 0x0f) * 4;
    uint32_t total = rd16(frame + 16);
    uint32_t destination = rd32(frame + 30);
    if (header < IP_HDR || total < header || ETH_HDR + total > length ||
        (rd16(frame + 20) & 0x3fff) != 0) {      /* no fragments */
        return;
    }
    if (local_ip != 0 && destination != local_ip &&
        destination != 0xffffffffU) {
        return;
    }
    uint32_t source = rd32(frame + 26);
    const uint8_t *payload = frame + ETH_HDR + header;
    uint32_t payload_length = total - header;
    switch (frame[23]) {
    case PROTO_ICMP: icmp_input(frame, source, payload, payload_length); break;
    case PROTO_UDP: udp_input(payload, payload_length); break;
    case PROTO_TCP: tcp_input(source, payload, payload_length); break;
    default: break;
    }
}

void net_poll(void)
{
    if (!have_nic) {
        return;
    }
    for (uint32_t budget = 0; budget < 32; ++budget) {
        uint16_t length = nic.receive(&nic, rx_buf, sizeof(rx_buf));
        if (length == 0) {
            return;
        }
        ++rx_frames;
        if (length < ETH_HDR) {
            continue;
        }
        switch (rd16(rx_buf + 12)) {
        case 0x0806: arp_input(rx_buf, length); break;
        case 0x0800: ipv4_input(rx_buf, length); break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Shared NIC helpers                                                  */
/* ------------------------------------------------------------------ */

void *nic_alloc_dma(uint32_t pages)
{
    return pmm_alloc_pages(pages);
}

void nic_pci_enable(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t command = pci_config_read32(bus, device, function, 0x04);
    /* I/O space, memory space, bus mastering. */
    pci_config_write32(bus, device, function, 0x04,
                       (command & 0x0000ffffU) | 0x0007U);
}

void nic_delay(uint32_t loops)
{
    for (uint32_t i = 0; i < loops; ++i) {
        __asm__ volatile("pause");
    }
}

/* ------------------------------------------------------------------ */
/* Initialisation and information                                      */
/* ------------------------------------------------------------------ */

bool net_init(void)
{
    have_nic = false;
    configured = false;
    for (uint32_t d = 0; d < NIC_DRIVER_COUNT; ++d) {
        registry_entries[d].name = nic_drivers[d]->name;
        registry_entries[d].bus = PERWIX_BUS_PCI;
        (void)driver_register(&registry_entries[d]);
    }
    for (uint32_t i = 0; i < pci_device_count() && !have_nic; ++i) {
        uint8_t bus, device, function, class_code, subclass, interface;
        uint16_t vendor_id, device_id;
        if (!pci_device_get(i, &bus, &device, &function, &vendor_id,
                            &device_id, &class_code, &subclass, &interface) ||
            class_code != 0x02) {
            continue;
        }
        for (uint32_t d = 0; d < NIC_DRIVER_COUNT; ++d) {
            if (!nic_drivers[d]->match(vendor_id, device_id)) {
                continue;
            }
            mzero(&nic, sizeof(nic));
            nic.name = nic_drivers[d]->name;
            if (nic_drivers[d]->init(bus, device, function, &nic)) {
                have_nic = true;
                break;
            }
        }
    }
    return have_nic;
}

bool net_ready(void) { return have_nic; }
uint32_t net_received_frames(void) { return rx_frames; }
uint32_t net_sent_frames(void) { return tx_frames; }
const netdev_t *net_device(void) { return have_nic ? &nic : NULL; }
uint32_t net_ip(void) { return local_ip; }
uint32_t net_netmask(void) { return netmask; }
uint32_t net_gateway(void) { return gateway; }
uint32_t net_dns_server(void) { return dns_server; }
bool net_dhcp_used(void) { return dhcp_ok; }

uint32_t net_arp_entry_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        count += arp_cache[i].used ? 1U : 0U;
    }
    return count;
}

bool net_arp_entry_get(uint32_t index, uint32_t *ip, uint8_t mac[6])
{
    uint32_t seen = 0;
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (!arp_cache[i].used) {
            continue;
        }
        if (seen++ == index) {
            *ip = arp_cache[i].ip;
            mcopy(mac, arp_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

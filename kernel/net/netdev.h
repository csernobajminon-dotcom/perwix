#ifndef PERWIX_NETDEV_H
#define PERWIX_NETDEV_H

#include <stdbool.h>
#include <stdint.h>

#define NETDEV_FRAME_MAX 1600

/* Network interface: a NIC driver fills this in, the stack uses it. */
typedef struct netdev {
    const char *name;               /* driver name, e.g. "rtl8139" */
    uint8_t mac[6];
    bool (*send)(struct netdev *dev, const uint8_t *frame, uint16_t length);
    /* Fetch one received frame into buffer; returns its length or 0. */
    uint16_t (*receive)(struct netdev *dev, uint8_t *buffer, uint16_t capacity);
    bool (*link_up)(struct netdev *dev);
    void *priv;                     /* driver private state */
} netdev_t;

/* A NIC driver: matched against PCI vendor/device, then initialised. */
typedef struct {
    const char *name;
    bool (*match)(uint16_t vendor, uint16_t device);
    bool (*init)(uint8_t bus, uint8_t device, uint8_t function,
                 netdev_t *dev);
} nic_driver_t;

extern const nic_driver_t nic_driver_e1000;
extern const nic_driver_t nic_driver_rtl8139;
extern const nic_driver_t nic_driver_rtl8169;
extern const nic_driver_t nic_driver_virtio;
extern const nic_driver_t nic_driver_pcnet;
extern const nic_driver_t nic_driver_ne2k;

/* Shared helpers for NIC drivers. */
void *nic_alloc_dma(uint32_t pages);   /* zeroed, contiguous, below 4 GiB */
void nic_pci_enable(uint8_t bus, uint8_t device, uint8_t function);
void nic_delay(uint32_t loops);

/* Stack information used by the shell. */
const netdev_t *net_device(void);
uint32_t net_ip(void);
uint32_t net_netmask(void);
uint32_t net_gateway(void);
uint32_t net_dns_server(void);
bool net_dhcp_used(void);
uint32_t net_sent_frames(void);
uint32_t net_arp_entry_count(void);
bool net_arp_entry_get(uint32_t index, uint32_t *ip, uint8_t mac[6]);

/* Stack operations. */
bool net_configure(void);               /* DHCP, falls back to QEMU defaults */
bool net_dhcp(void);
bool net_resolve(const char *host, uint32_t *address); /* name or dotted IP */
bool net_ping_time(uint32_t address, uint32_t *rtt_ms);
/* Plain HTTP/1.0 GET; body+headers are passed to sink chunk by chunk. */
int net_http_get(const char *host, const char *path,
                 void (*sink)(const char *data, uint32_t length));

#endif

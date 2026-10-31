#include "perwix.h"

#define XHCI_USBCMD 0x00
#define XHCI_USBSTS 0x04
#define XHCI_PORTSC 0x400

static uint32_t controllers_found;
static uint32_t connected_ports;
static bool xhci_ready;

static bool initialize_xhci(uint8_t bus, uint8_t device, uint8_t function)
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

    uint32_t command = pci_config_read32(bus, device, function, 0x04);
    pci_config_write32(bus, device, function, 0x04,
                       (command & 0xffff0000U) | ((command & 0xffffU) | 0x0006U));

    volatile uint32_t *capability = (volatile uint32_t *)(uintptr_t)bar;
    uint8_t capability_length = (uint8_t)(capability[0] & 0xff);
    uint32_t structural = capability[1];
    uint8_t port_count = (uint8_t)(structural >> 24);
    if (capability_length < 0x20 || port_count == 0 || port_count > 64) {
        return false;
    }

    volatile uint32_t *operational =
        (volatile uint32_t *)(uintptr_t)(bar + capability_length);
    operational[XHCI_USBCMD / 4] &= ~1U;
    bool halted = false;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if ((operational[XHCI_USBSTS / 4] & 1) != 0) {
            halted = true;
            break;
        }
    }
    if (!halted) {
        return false;
    }

    operational[XHCI_USBCMD / 4] |= 2;
    bool reset_complete = false;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        uint32_t status = operational[XHCI_USBSTS / 4];
        if ((operational[XHCI_USBCMD / 4] & 2) == 0 &&
            (status & (1U << 11)) == 0) {
            reset_complete = true;
            break;
        }
    }
    if (!reset_complete) {
        return false;
    }

    connected_ports = 0;
    volatile uint32_t *ports =
        (volatile uint32_t *)(uintptr_t)(bar + capability_length + XHCI_PORTSC);
    for (uint32_t port = 0; port < port_count; ++port) {
        if ((ports[port * 4] & 1) != 0) {
            ++connected_ports;
        }
    }
    return true;
}

bool usb_init(void)
{
    controllers_found = 0;
    connected_ports = 0;
    xhci_ready = false;

    for (uint32_t i = 0; i < pci_device_count(); ++i) {
        uint8_t bus, device, function, class_code, subclass, interface;
        uint16_t vendor_id, device_id;
        if (!pci_device_get(i, &bus, &device, &function, &vendor_id,
                            &device_id, &class_code, &subclass, &interface) ||
            class_code != 0x0c || subclass != 0x03) {
            continue;
        }
        ++controllers_found;
        if (interface == 0x30 && initialize_xhci(bus, device, function)) {
            xhci_ready = true;
        }
    }
    return xhci_ready;
}

uint32_t usb_controller_count(void)
{
    return controllers_found;
}

bool usb_xhci_ready(void)
{
    return xhci_ready;
}

uint32_t usb_connected_port_count(void)
{
    return connected_ports;
}

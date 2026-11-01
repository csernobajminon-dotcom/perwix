#include "perwix.h"

/* PCI programming interfaces: UHCI=00, OHCI=10, EHCI=20, xHCI=30. */
#define USB_PI_UHCI 0x00
#define USB_PI_OHCI 0x10
#define USB_PI_EHCI 0x20
#define USB_PI_XHCI 0x30

#define XHCI_USBCMD 0x00
#define XHCI_USBSTS 0x04
#define XHCI_PORTSC 0x400

#define EHCI_USBCMD 0x00
#define EHCI_USBSTS 0x04
#define EHCI_STS_HALTED (1U << 12)
#define EHCI_CMD_RUN (1U << 0)
#define EHCI_CMD_RESET (1U << 1)

#define OHCI_REVISION 0x00
#define OHCI_CONTROL 0x04
#define OHCI_COMMAND_STATUS 0x08
#define OHCI_INTERRUPT_DISABLE 0x14
#define OHCI_COMMAND_RESET (1U << 0)

#define UHCI_USBCMD 0x00
#define UHCI_USBSTS 0x02
#define UHCI_CMD_RESET (1U << 1)

static uint32_t controllers_found;
static uint32_t controllers_ready;
static uint32_t connected_ports;
static uint32_t family_count[4];
static uint32_t family_ready[4];
static bool xhci_ready;

static int family_index(uint8_t programming_interface)
{
    if (programming_interface == USB_PI_UHCI) return 0;
    if (programming_interface == USB_PI_OHCI) return 1;
    if (programming_interface == USB_PI_EHCI) return 2;
    if (programming_interface == USB_PI_XHCI) return 3;
    return -1;
}

static void enable_pci_io(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t command = pci_config_read32(bus, device, function, 0x04);
    pci_config_write32(bus, device, function, 0x04,
                       (command & 0xffffU) | 0x0005U); /* I/O + bus master */
}

static void enable_pci_memory(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t command = pci_config_read32(bus, device, function, 0x04);
    pci_config_write32(bus, device, function, 0x04,
                       (command & 0xffffU) | 0x0006U); /* memory + bus master */
}

/* USB 1.x UHCI: controller reset over its PCI I/O BAR. */
static bool initialize_uhci(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x20);
    if ((bar & 1U) == 0) {
        return false;
    }
    uint16_t base = (uint16_t)(bar & 0xfffcU);
    if (base == 0 || base == 0xfffcU) {
        return false;
    }

    enable_pci_io(bus, device, function);
    outw((uint16_t)(base + UHCI_USBCMD), UHCI_CMD_RESET);
    bool reset = false;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if ((inw((uint16_t)(base + UHCI_USBCMD)) & UHCI_CMD_RESET) == 0) {
            reset = true;
            break;
        }
    }
    if (!reset) {
        return false;
    }
    outw((uint16_t)(base + UHCI_USBCMD), 0);
    outw((uint16_t)(base + UHCI_USBSTS), 0xffffU);
    return true;
}

/* USB 1.x OHCI: halt schedules, issue HCR and wait for reset completion. */
static bool initialize_ohci(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x10);
    if ((bar & 1U) != 0) {
        return false;
    }
    uint64_t address = (uint64_t)(bar & ~0x0fU);
    if (((bar >> 1) & 3U) == 2U) {
        address |= (uint64_t)pci_config_read32(bus, device, function, 0x14) << 32;
    }
    if (address == 0 || address >= 0x100000000ULL) {
        return false;
    }

    enable_pci_memory(bus, device, function);
    volatile uint32_t *regs = (volatile uint32_t *)(uintptr_t)address;
    uint32_t revision = regs[OHCI_REVISION / 4] & 0xffU;
    if (revision == 0 || revision == 0xffU) {
        return false;
    }

    regs[OHCI_CONTROL / 4] = 0;
    regs[OHCI_INTERRUPT_DISABLE / 4] = 0xffffffffU;
    regs[OHCI_COMMAND_STATUS / 4] = OHCI_COMMAND_RESET;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if ((regs[OHCI_COMMAND_STATUS / 4] & OHCI_COMMAND_RESET) == 0) {
            regs[OHCI_CONTROL / 4] = 0; /* remain in the USB reset state */
            return true;
        }
    }
    return false;
}

/* USB 2.0 EHCI: stop the run bit, wait for HALTED, then reset the HC. */
static bool initialize_ehci(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t bar = pci_config_read32(bus, device, function, 0x10);
    if ((bar & 1U) != 0) {
        return false;
    }
    uint64_t address = (uint64_t)(bar & ~0x0fU);
    if (((bar >> 1) & 3U) == 2U) {
        address |= (uint64_t)pci_config_read32(bus, device, function, 0x14) << 32;
    }
    if (address == 0 || address >= 0x100000000ULL) {
        return false;
    }

    enable_pci_memory(bus, device, function);
    volatile uint8_t *capability = (volatile uint8_t *)(uintptr_t)address;
    uint8_t capability_length = capability[0];
    if (capability_length < 0x10 || capability_length > 0x40) {
        return false;
    }
    volatile uint32_t *operational =
        (volatile uint32_t *)(uintptr_t)(address + capability_length);

    operational[EHCI_USBCMD / 4] &= ~EHCI_CMD_RUN;
    bool halted = false;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if ((operational[EHCI_USBSTS / 4] & EHCI_STS_HALTED) != 0) {
            halted = true;
            break;
        }
    }
    if (!halted) {
        return false;
    }

    operational[EHCI_USBCMD / 4] |= EHCI_CMD_RESET;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        if ((operational[EHCI_USBCMD / 4] & EHCI_CMD_RESET) == 0) {
            return true;
        }
    }
    return false;
}

/* USB 3.x xHCI: stop, reset and count connected root-hub ports. */
static bool initialize_xhci(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t bar_low = pci_config_read32(bus, device, function, 0x10);
    if ((bar_low & 1U) != 0) {
        return false;
    }
    uint64_t bar = bar_low & ~0x0fU;
    if (((bar_low >> 1) & 3U) == 2U) {
        bar |= (uint64_t)pci_config_read32(bus, device, function, 0x14) << 32;
    }
    if (bar == 0 || bar >= 0x100000000ULL) {
        return false;
    }

    enable_pci_memory(bus, device, function);
    volatile uint32_t *capability = (volatile uint32_t *)(uintptr_t)bar;
    uint8_t capability_length = (uint8_t)(capability[0] & 0xffU);
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
        if ((operational[XHCI_USBSTS / 4] & 1U) != 0) {
            halted = true;
            break;
        }
    }
    if (!halted) {
        return false;
    }

    operational[XHCI_USBCMD / 4] |= 2U;
    bool reset_complete = false;
    for (uint32_t wait = 0; wait < 1000000; ++wait) {
        uint32_t status = operational[XHCI_USBSTS / 4];
        if ((operational[XHCI_USBCMD / 4] & 2U) == 0 &&
            (status & (1U << 11)) == 0) {
            reset_complete = true;
            break;
        }
    }
    if (!reset_complete) {
        return false;
    }

    volatile uint32_t *ports =
        (volatile uint32_t *)(uintptr_t)(bar + capability_length + XHCI_PORTSC);
    for (uint32_t port = 0; port < port_count; ++port) {
        if ((ports[port * 4] & 1U) != 0) {
            ++connected_ports;
        }
    }
    return true;
}

bool usb_init(void)
{
    controllers_found = 0;
    controllers_ready = 0;
    connected_ports = 0;
    xhci_ready = false;
    for (uint32_t i = 0; i < 4; ++i) {
        family_count[i] = 0;
        family_ready[i] = 0;
    }

    for (uint32_t i = 0; i < pci_device_count(); ++i) {
        uint8_t bus, device, function, class_code, subclass, interface;
        uint16_t vendor_id, device_id;
        if (!pci_device_get(i, &bus, &device, &function, &vendor_id,
                            &device_id, &class_code, &subclass, &interface) ||
            class_code != 0x0c || subclass != 0x03) {
            continue;
        }
        (void)vendor_id;
        (void)device_id;
        ++controllers_found;
        int family = family_index(interface);
        if (family < 0) {
            continue;
        }
        ++family_count[family];

        bool ready = false;
        switch (interface) {
        case USB_PI_UHCI: ready = initialize_uhci(bus, device, function); break;
        case USB_PI_OHCI: ready = initialize_ohci(bus, device, function); break;
        case USB_PI_EHCI: ready = initialize_ehci(bus, device, function); break;
        case USB_PI_XHCI:
            ready = initialize_xhci(bus, device, function);
            if (ready) xhci_ready = true;
            break;
        default: break;
        }
        if (ready) {
            ++controllers_ready;
            ++family_ready[family];
        }
    }
    return controllers_ready != 0;
}

uint32_t usb_controller_count(void)
{
    return controllers_found;
}

uint32_t usb_controller_ready_count(void)
{
    return controllers_ready;
}

uint32_t usb_controller_family_count(uint8_t programming_interface)
{
    int family = family_index(programming_interface);
    return family < 0 ? 0 : family_count[family];
}

uint32_t usb_controller_family_ready_count(uint8_t programming_interface)
{
    int family = family_index(programming_interface);
    return family < 0 ? 0 : family_ready[family];
}

bool usb_xhci_ready(void)
{
    return xhci_ready;
}

uint32_t usb_connected_port_count(void)
{
    return connected_ports;
}

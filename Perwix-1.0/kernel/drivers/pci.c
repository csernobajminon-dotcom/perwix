#include "perwix.h"

#define PCI_CONFIG_ADDRESS 0x0cf8
#define PCI_CONFIG_DATA 0x0cfc
#define PCI_DEVICE_LIMIT 128

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint16_t vendor_id;
    uint16_t device_id;
} pci_device_t;

static pci_device_t devices[PCI_DEVICE_LIMIT];
static uint32_t device_count;

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset)
{
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) | ((uint32_t)function << 8) |
        (offset & 0xfcU);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint32_t value)
{
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) | ((uint32_t)function << 8) |
        (offset & 0xfcU);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static void add_device(uint8_t bus, uint8_t device, uint8_t function,
                       uint32_t identity, uint32_t class_info)
{
    if (device_count == PCI_DEVICE_LIMIT) {
        return;
    }
    devices[device_count++] = (pci_device_t) {
        .bus = bus,
        .device = device,
        .function = function,
        .vendor_id = (uint16_t)identity,
        .device_id = (uint16_t)(identity >> 16),
        .class_code = (uint8_t)(class_info >> 24),
        .subclass = (uint8_t)(class_info >> 16),
        .programming_interface = (uint8_t)(class_info >> 8),
    };
}

void pci_init(void)
{
    device_count = 0;
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t slot = 0; slot < 32; ++slot) {
            uint32_t identity0 = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0);
            if ((uint16_t)identity0 == 0xffff) {
                continue;
            }
            uint32_t header = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0c);
            uint8_t functions = (((header >> 16) & 0x80) != 0) ? 8 : 1;
            for (uint8_t function = 0; function < functions; ++function) {
                uint32_t identity = function == 0 ? identity0 :
                    pci_config_read32((uint8_t)bus, (uint8_t)slot, function, 0);
                if ((uint16_t)identity == 0xffff) {
                    continue;
                }
                uint32_t class_info = pci_config_read32(
                    (uint8_t)bus, (uint8_t)slot, function, 0x08);
                add_device((uint8_t)bus, (uint8_t)slot, function,
                           identity, class_info);
            }
        }
    }
}

uint32_t pci_device_count(void)
{
    return device_count;
}

bool pci_device_get(uint32_t index, uint8_t *bus, uint8_t *device,
                    uint8_t *function, uint16_t *vendor_id,
                    uint16_t *device_id, uint8_t *class_code,
                    uint8_t *subclass, uint8_t *programming_interface)
{
    if (index >= device_count) {
        return false;
    }
    const pci_device_t *entry = &devices[index];
    *bus = entry->bus;
    *device = entry->device;
    *function = entry->function;
    *vendor_id = entry->vendor_id;
    *device_id = entry->device_id;
    *class_code = entry->class_code;
    *subclass = entry->subclass;
    *programming_interface = entry->programming_interface;
    return true;
}

const char *usb_controller_name(uint8_t class_code, uint8_t subclass,
                                uint8_t programming_interface)
{
    if (class_code != 0x0c || subclass != 0x03) {
        return 0;
    }
    switch (programming_interface) {
    case 0x00: return "UHCI";
    case 0x10: return "OHCI";
    case 0x20: return "EHCI";
    case 0x30: return "xHCI";
    default: return "USB host controller";
    }
}

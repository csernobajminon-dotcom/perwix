#include "perwix.h"

#define MULTIBOOT2_BOOT_MAGIC 0x36d76289

static void boot_status(uint8_t color, const char *message)
{
    console_set_color(color);
    console_write("[ ");
    console_write(color == 0x0e ? "INFO" : " OK ");
    console_write(" ] ");
    console_set_color(0x07);
    console_write(message);
    console_write("\n");
}

static bool command_line_has_option(uint32_t multiboot_info_address,
                                    const char *wanted)
{
    if (multiboot_info_address == 0 || wanted == 0 || *wanted == '\0') {
        return false;
    }

    const uint8_t *info = (const uint8_t *)(uintptr_t)multiboot_info_address;
    const uint32_t total_size = *(const uint32_t *)info;
    if (total_size < 16U || total_size > 16U * 1024U * 1024U) {
        return false;
    }

    uint32_t offset = 8U;
    while (offset <= total_size - 8U) {
        const uint32_t *tag = (const uint32_t *)(info + offset);
        const uint32_t type = tag[0];
        const uint32_t size = tag[1];
        if (size < 8U || size > total_size - offset) {
            break;
        }
        if (type == 0U) {
            break;
        }
        if (type == 1U) {
            const char *command_line = (const char *)(info + offset + 8U);
            const uint32_t command_length = size - 8U;
            uint32_t pos = 0;
            while (pos < command_length) {
                while (pos < command_length &&
                       (command_line[pos] == ' ' || command_line[pos] == '\t')) {
                    ++pos;
                }
                const uint32_t start = pos;
                while (pos < command_length && command_line[pos] != '\0' &&
                       command_line[pos] != ' ' && command_line[pos] != '\t') {
                    ++pos;
                }
                uint32_t wanted_pos = 0;
                while (start + wanted_pos < pos && wanted[wanted_pos] != '\0' &&
                       command_line[start + wanted_pos] == wanted[wanted_pos]) {
                    ++wanted_pos;
                }
                if (start + wanted_pos == pos && wanted[wanted_pos] == '\0') {
                    return true;
                }
                if (pos < command_length && command_line[pos] == '\0') {
                    break;
                }
            }
        }
        const uint32_t aligned_size = (size + 7U) & ~7U;
        if (aligned_size > total_size - offset) {
            break;
        }
        offset += aligned_size;
    }
    return false;
}

void kernel_main(uint32_t boot_magic, uint32_t multiboot_info_address)
{
    console_init();

    if (boot_magic != MULTIBOOT2_BOOT_MAGIC) {
        console_set_color(0x0c);
        console_write("[ FAIL ] Invalid GRUB2 boot signature\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    const bool maintenance_mode =
        command_line_has_option(multiboot_info_address, "single");
    const bool safe_mode =
        command_line_has_option(multiboot_info_address, "safe");
    const bool verbose_boot =
        command_line_has_option(multiboot_info_address, "verbose");
    const bool reduced_hardware_init = maintenance_mode || safe_mode;

    pmm_init(multiboot_info_address);
    console_set_color(0x0b);
    console_write("Perwix 1.1 amd64 - BSD-style boot\n");
    console_set_color(0x07);

    if (maintenance_mode) {
        boot_status(0x0e, "single-user maintenance shell requested");
    }
    if (safe_mode) {
        boot_status(0x0e, "safe mode: network, USB and mouse initialization skipped");
    }

    interrupts_init();
    pci_init();
    bool network_ready = reduced_hardware_init ? false : net_init();
    bool usb_ready = reduced_hardware_init ? false : usb_init();
    keyboard_init();
    if (!reduced_hardware_init) {
        (void)mouse_init();
    }
    ipc_init();
    scheduler_init();
    timer_init();

    if (verbose_boot) {
        boot_status(0x0a, "GRUB2 / Multiboot2 boot protocol");
        boot_status(0x0a, "bootstrap memory allocator initialized");
        console_set_color(0x0a);
        console_write("[  OK  ] ");
        console_set_color(0x07);
        console_write("PCI scan: ");
        console_write_u64(pci_device_count());
        console_write(" device(s) found\n");
        boot_status(network_ready ? 0x0a : 0x0e,
                    network_ready ? "network interface initialized (see `ifconfig`)" :
                                    "no supported Ethernet interface / disabled by boot mode");
        boot_status(0x0a, "PS/2 keyboard driver ready");
        if (!reduced_hardware_init) {
            boot_status(mouse_has_wheel() ? 0x0a : 0x0e,
                        mouse_has_wheel() ? "PS/2 wheel and scrollback ready" :
                                            "Mouse wheel unavailable; keyboard ready");
        }
        boot_status(0x0a, "PIC/PIT 100 Hz and round-robin scheduler ready");
        boot_status(0x0a, "kernel IPC queues ready");
        boot_status(usb_ready ? 0x0a : 0x0e,
                    usb_ready ? "USB host controller(s) reset; see `usbinfo`" :
                                "no USB host controller initialized / disabled by boot mode");
    } else {
        boot_status(0x0a, "kernel services initialized; type `help` for commands");
    }

    irq_enable();
    shell_run();
}

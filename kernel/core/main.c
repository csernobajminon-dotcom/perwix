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

void kernel_main(uint32_t boot_magic, uint32_t multiboot_info_address)
{
    console_init();
    console_draw_logo();

    if (boot_magic != MULTIBOOT2_BOOT_MAGIC) {
        console_set_color(0x0c);
        console_write("[ FAIL ] Invalid GRUB2 boot signature\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    pmm_init(multiboot_info_address);
    console_set_color(0x0b);
    console_write("Perwix 1.0 amd64 - BSD-style boot\n");
    console_set_color(0x07);
    boot_status(0x0a, "GRUB2 / Multiboot2 boot protocol");
    boot_status(0x0a, "bootstrap memory allocator initialized");

    interrupts_init();
    pci_init();
    bool network_ready = net_init();
    bool usb_ready = usb_init();
    keyboard_init();
    (void)mouse_init();
    ipc_init();
    scheduler_init();
    timer_init();

    console_set_color(0x0a);
    console_write("[  OK  ] ");
    console_set_color(0x07);
    console_write("PCI scan: ");
    console_write_u64(pci_device_count());
    console_write(" device(s) found\n");
    boot_status(network_ready ? 0x0a : 0x0e,
                network_ready ? "e1000 network interface initialized" :
                                "no supported Ethernet interface");
    boot_status(0x0a, "PS/2 keyboard driver ready");
    boot_status(mouse_has_wheel() ? 0x0a : 0x0e,
                mouse_has_wheel() ? "PS/2 wheel and scrollback ready" :
                                    "Mouse wheel unavailable; keyboard ready");
    boot_status(0x0a, "PIC/PIT 100 Hz and round-robin scheduler ready");
    boot_status(0x0a, "kernel IPC queues ready");
    boot_status(usb_ready ? 0x0a : 0x0e,
                usb_ready ? "xHCI host controller reset and ready" :
                            "USB host controller not initialized");

    irq_enable();
    shell_run();
}

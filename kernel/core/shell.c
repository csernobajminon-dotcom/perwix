#include "perwix.h"
#include "vfs.h"
#include "netdev.h"

static bool text_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) {
            return false;
        }
    }
    return *left == *right;
}

static void print_hex_digit(uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[2] = { digits[value & 0x0f], '\0' };
    console_write(text);
}

static void print_hex16(uint16_t value)
{
    print_hex_digit((uint8_t)(value >> 12));
    print_hex_digit((uint8_t)(value >> 8));
    print_hex_digit((uint8_t)(value >> 4));
    print_hex_digit((uint8_t)value);
}

static void print_cpu_vendor(void)
{
    uint32_t eax = 0, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    char vendor[13];
    vendor[0] = (char)ebx;
    vendor[1] = (char)(ebx >> 8);
    vendor[2] = (char)(ebx >> 16);
    vendor[3] = (char)(ebx >> 24);
    vendor[4] = (char)edx;
    vendor[5] = (char)(edx >> 8);
    vendor[6] = (char)(edx >> 16);
    vendor[7] = (char)(edx >> 24);
    vendor[8] = (char)ecx;
    vendor[9] = (char)(ecx >> 8);
    vendor[10] = (char)(ecx >> 16);
    vendor[11] = (char)(ecx >> 24);
    vendor[12] = '\0';
    console_write("Architecture: x86-64\nCPU vendor: ");
    console_write(vendor);
    console_write("\n");
}

static void print_boot_help(void)
{
    console_set_color(0x0b);
    console_write("Perwix commands\n");
    console_set_color(0x07);
    console_write("help       show this command list\n");
    console_write("about, version, arch, cpu   system identity\n");
    console_write("mem, free, uptime, tasks, ps\n");
    console_write("pci, lspci, usb, lsusb, usbinfo, pciinfo, drivers, drivercheck\n");
    console_write("mouse, fs, ls, pwd, whoami, echo TEXT, clear, logo\n");
    console_write("Reboot/poweroff require an arrow-key confirmation.\n");
    extra_help();
}

static void print_pci_devices(bool only_usb)
{
    uint32_t found = 0;
    for (uint32_t i = 0; i < pci_device_count(); ++i) {
        uint8_t bus, device, function, class_code, subclass, interface;
        uint16_t vendor_id, device_id;
        if (!pci_device_get(i, &bus, &device, &function, &vendor_id,
                            &device_id, &class_code, &subclass, &interface)) {
            continue;
        }
        const char *controller =
            usb_controller_name(class_code, subclass, interface);
        if (only_usb && controller == 0) {
            continue;
        }
        ++found;
        console_set_color(0x0b);
        console_write("PCI ");
        print_hex_digit(bus >> 4);
        print_hex_digit(bus);
        console_write(":");
        print_hex_digit(device >> 4);
        print_hex_digit(device);
        console_write(".");
        print_hex_digit(function);
        console_set_color(0x07);
        console_write("  ");
        if (controller != 0) {
            console_write(controller);
            console_write(" ");
        }
        print_hex16(vendor_id);
        console_write(":");
        print_hex16(device_id);
        console_write("  class ");
        print_hex_digit(class_code >> 4);
        print_hex_digit(class_code);
        console_write(":");
        print_hex_digit(subclass >> 4);
        print_hex_digit(subclass);
        console_write("\n");
    }
    if (found == 0) {
        console_write(only_usb ? "No USB host controller found.\n" :
                                "No PCI devices found.\n");
    }
}

static void print_drivers(void)
{
    console_write("Active drivers and services:\n");
    console_set_color(0x0a);
    console_write("[ready] ");
    console_set_color(0x07);
    console_write("VGA text console\n");
    console_set_color(0x0a);
    console_write("[ready] ");
    console_set_color(0x07);
    console_write("COM1 serial console\n");
    console_set_color(0x0a);
    console_write("[ready] ");
    console_set_color(0x07);
    console_write("PS/2 keyboard input\n");
    console_set_color(0x0a);
    console_write("[ready] ");
    console_set_color(0x07);
    console_write("8259 PIC and 100 Hz PIT\n");
    console_set_color(0x0a);
    console_write("[ready] ");
    console_set_color(0x07);
    console_write("PCI configuration and device scan\n");
    if (net_ready()) {
        console_set_color(0x0a);
        console_write("[ready] ");
        console_set_color(0x07);
        console_write(net_device()->name);
        console_write(" NIC + ARP/IPv4/ICMP/UDP/DHCP/DNS/TCP/HTTP\n");
    } else {
        console_set_color(0x0e);
        console_write("[idle]  ");
        console_set_color(0x07);
        console_write("NIC drivers: e1000 rtl8139 rtl8169 virtio-net pcnet32 ne2k-pci\n");
    }
    console_set_color(usb_controller_ready_count() != 0 ? 0x0a : 0x0e);
    console_write(usb_controller_ready_count() != 0 ? "[ready] " : "[idle]  ");
    console_set_color(0x07);
    console_write("UHCI/OHCI/EHCI/xHCI controller reset and probe\n");
    console_set_color(0x0e);
    console_write("[planned] ");
    console_set_color(0x07);
    console_write("USB transfer rings/schedules, enumeration and HID\n");
    console_set_color(0x0c);
    console_write("[planned] ");
    console_set_color(0x07);
    console_write("disk, graphics, audio, Wi-Fi and USB device drivers\n");
}

static void print_tasks(void)
{
    console_write("Kernel tasks:\n");
    for (uint32_t id = 0; id < PERWIX_TASK_COUNT; ++id) {
        console_write("  ");
        console_write_u64(id);
        console_write("  ");
        console_write(scheduler_task_name(id));
        if (id == scheduler_current_id()) {
            console_write("  (current)");
        }
        console_write("\n");
    }
}

static void print_memory(void)
{
    uint64_t pages = pmm_bootstrap_pages();
    console_write("Bootstrap allocator: ");
    console_write_u64(pages);
    console_write(" free pages (4 KiB each), about ");
    console_write_u64((pages * 4) / 1024);
    console_write(" MiB\n");
}

static void print_uptime(void)
{
    uint64_t ticks = system_ticks;
    console_write("Uptime: ");
    console_write_u64(ticks / PERWIX_TIMER_HZ);
    console_write(" seconds (PIT ticks: ");
    console_write_u64(ticks);
    console_write(")\n");
}

static bool dangerous_command(const char *command)
{
    return text_equal(command, "reboot") ||
           text_equal(command, "poweroff") ||
           text_equal(command, "shutdown") ||
           text_equal(command, "halt");
}

static void draw_confirmation_choices(bool yes_selected)
{
    console_write("  ");
    if (yes_selected) {
        console_set_color(0x0a);
        console_write("[ YES ]");
    } else {
        console_set_color(0x08);
        console_write("( YES )");
    }
    console_set_color(0x07);
    console_write("      ");
    if (yes_selected) {
        console_set_color(0x08);
        console_write("(  NO  )");
    } else {
        console_set_color(0x0c);
        console_write("[  NO  ]");
    }
    console_set_color(0x07);
}

static bool confirm_dangerous_action(const char *command)
{
    bool yes_selected = false; /* Safe default: NO. */
    console_write("\n+------------------------------------------+\n");
    console_set_color(0x0c);
    console_write("|           PERWIX SECURITY CHECK          |\n");
    console_set_color(0x07);
    console_write("| Confirm dangerous action: ");
    console_write(command);
    console_write("\n");
    console_write("| Use LEFT/RIGHT or UP/DOWN; ENTER selects.\n");
    console_write("| ESC cancels. Default selection is NO.\n");
    draw_confirmation_choices(yes_selected);

    for (;;) {
        char character = '\0';
        uint8_t key = keyboard_read_key(&character);
        if (key == PERWIX_KEY_LEFT || key == PERWIX_KEY_UP) {
            if (!yes_selected) {
                yes_selected = true;
                console_write("\r");
                draw_confirmation_choices(yes_selected);
            }
        } else if (key == PERWIX_KEY_RIGHT || key == PERWIX_KEY_DOWN) {
            if (yes_selected) {
                yes_selected = false;
                console_write("\r");
                draw_confirmation_choices(yes_selected);
            }
        } else if (key == PERWIX_KEY_ESCAPE) {
            console_write("\n+------------------------------------------+\n");
            return false;
        } else if (key == PERWIX_KEY_CHAR && character == '\n') {
            console_write("\n+------------------------------------------+\n");
            return yes_selected;
        } else {
            mouse_poll();
            __asm__ volatile("pause");
        }
    }
}

static void run_command(char *line)
{
    char *command = line;
    while (*command == ' ' || *command == '\t') {
        ++command;
    }
    if (*command == '\0') {
        return;
    }
    char *argument = command;
    while (*argument != '\0' && *argument != ' ' && *argument != '\t') {
        if (*argument >= 'A' && *argument <= 'Z') {
            *argument = (char)(*argument - 'A' + 'a');
        }
        ++argument;
    }
    if (*argument != '\0') {
        *argument++ = '\0';
        while (*argument == ' ' || *argument == '\t') {
            ++argument;
        }
    }

    if (dangerous_command(command) && !confirm_dangerous_action(command)) {
        console_write("[SECURITY] Action cancelled; nothing was changed.\n");
        return;
    }

    if (text_equal(command, "help")) {
        print_boot_help();
    } else if (text_equal(command, "clear")) {
        console_clear();
    } else if (text_equal(command, "about")) {
        console_write("Perwix is an x86-64 PC kernel with a small built-in shell.\n");
        console_write("This is an early system; commands report implemented features.\n");
    } else if (text_equal(command, "version") || text_equal(command, "uname")) {
        console_write("Perwix 1.1 | amd64 | GRUB2/Multiboot2\n");
    } else if (text_equal(command, "arch")) {
        console_write("x86-64 (AMD64)\n");
    } else if (text_equal(command, "cpu")) {
        print_cpu_vendor();
    } else if (text_equal(command, "mem")) {
        print_memory();
    } else if (text_equal(command, "free")) {
        print_memory();
    } else if (text_equal(command, "uptime")) {
        print_uptime();
    } else if (text_equal(command, "tasks")) {
        print_tasks();
    } else if (text_equal(command, "ps")) {
        print_tasks();
    } else if (text_equal(command, "mouse")) {
        console_write(mouse_has_wheel() ?
            "PS/2 wheel mouse active; scrollback lines: " :
            "PS/2 wheel mouse not detected; scrollback lines: ");
        console_write_u64(console_scrollback_lines());
        console_write("\n");
    } else if (text_equal(command, "pci") || text_equal(command, "lspci")) {
        print_pci_devices(false);
    } else if (text_equal(command, "usb") || text_equal(command, "lsusb")) {
        print_pci_devices(true);
        console_write("USB host controllers: ");
        console_write_u64(usb_controller_count());
        console_write("; controllers reset OK: ");
        console_write_u64(usb_controller_ready_count());
        console_write("; xHCI ");
        console_write(usb_xhci_ready() ? "reset OK" : "not reset");
        console_write("; xHCI connected ports: ");
        console_write_u64(usb_connected_port_count());
        console_write("\nUse usbinfo for controller generations and readiness.\n");
    } else if (text_equal(command, "drivers")) {
        print_drivers();
    } else if (text_equal(command, "fs")) {
        console_write("VFS registry: ");
        console_write_u64(vfs_registered_count());
        console_write(" filesystem types; mounted volumes: 0\n");
        console_write("Disk filesystems are not implemented yet.\n");
    } else if (text_equal(command, "pwd")) {
        console_write("/\n");
    } else if (text_equal(command, "ls")) {
        console_write("No filesystem is mounted. Use `fs` for status.\n");
    } else if (text_equal(command, "whoami")) {
        console_write("root (kernel console; user accounts not implemented)\n");
    } else if (text_equal(command, "echo")) {
        console_write(argument);
        console_write("\n");
    } else if (text_equal(command, "reboot")) {
        console_write("Rebooting...\n");
        __asm__ volatile("cli" : : : "memory");
        for (uint32_t wait = 0; wait < 100000; ++wait) {
            if ((inb(0x64) & 0x02) == 0) {
                outb(0x64, 0xfe);
                break;
            }
        }
        for (;;) {
            __asm__ volatile("hlt");
        }
    } else if (extra_command_run(command, argument)) {
        return;
    } else {
        console_set_color(0x0c);
        console_write("Unknown command: ");
        console_set_color(0x07);
        console_write(command);
        console_write(". Type help.\n");
    }
}

static uint32_t read_line(char *buffer, uint32_t capacity)
{
    uint32_t length = 0;
    for (;;) {
        char character;
        if (!keyboard_read_char(&character)) {
            mouse_poll();
            __asm__ volatile("pause");
            continue;
        }
        if (character == '\n') {
            console_write("\n");
            buffer[length] = '\0';
            return length;
        }
        if (character == '\b') {
            if (length != 0) {
                --length;
                console_write("\b");
            }
            continue;
        }
        if ((uint8_t)character >= 0x20 && length + 1 < capacity) {
            buffer[length++] = character;
            char echo[2] = { character, '\0' };
            console_write(echo);
        }
    }
}

void shell_run(void)
{
    console_set_color(0x0b);
    console_write("Welcome to Perwix. Type help to see available commands.\n");
    console_set_color(0x07);

    char line[128];
    for (;;) {
        console_set_color(0x0a);
        console_write("perwix");
        console_set_color(0x07);
        console_write("@pc:/ $ ");
        (void)read_line(line, sizeof(line));
        run_command(line);
    }
}

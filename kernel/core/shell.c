#include "perwix.h"
#include "vfs.h"
#include "netdev.h"
#include "module.h"
#include "../storage/storage.h"
#include "../lib/utf8.h"

static char current_directory[512] = "/";
static char file_output_buffer[65537];
static vfs_dirent_t directory_entries[VFS_MAX_DIR_ENTRIES];

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
    console_write("mouse, fs, mount, ls, cd, cat, pwd, whoami, clear, logo, modules\n");
    console_write("layout (Alt+Shift toggles US/UA), utf8test\n");
    console_write("ELF commands: hello, echo, uname, uptime, meminfo, diskpart\n");
    console_write("Reboot/poweroff require an arrow-key confirmation.\n");
    perwix_command_list();
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
        console_write("No registered NIC driver matched a PCI device\n");
    }
    console_write("[module] loadable ELF NIC drivers: ");
    console_write_u64(6);
    console_write(" built as separate .ko files\n");
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

static bool normalize_path(const char *argument, char *output, uint32_t capacity)
{
    char combined[1024];
    uint32_t length = 0U;
    uint32_t component_starts[128];
    uint32_t depth = 0U;

    if (argument == 0 || *argument == '\0') {
        argument = current_directory;
    }
    if (*argument == '/') {
        uint32_t i = 0U;
        while (argument[i] != '\0' && i + 1U < sizeof(combined)) {
            combined[i] = argument[i];
            ++i;
        }
        if (argument[i] != '\0') return false;
        combined[i] = '\0';
    } else {
        uint32_t i = 0U;
        while (current_directory[i] != '\0' && i + 1U < sizeof(combined)) {
            combined[i] = current_directory[i];
            ++i;
        }
        if (i + 1U >= sizeof(combined)) return false;
        if (i == 0U || combined[i - 1U] != '/') combined[i++] = '/';
        for (uint32_t j = 0U; argument[j] != '\0'; ++j) {
            if (i + 1U >= sizeof(combined)) return false;
            combined[i++] = argument[j];
        }
        combined[i] = '\0';
    }

    output[length++] = '/';
    uint32_t pos = 0U;
    while (combined[pos] == '/') ++pos;
    while (combined[pos] != '\0') {
        char component[ VFS_NAME_MAX ];
        uint32_t component_length = 0U;
        while (combined[pos] != '\0' && combined[pos] != '/') {
            if (component_length + 1U >= sizeof(component)) return false;
            component[component_length++] = combined[pos++];
        }
        component[component_length] = '\0';
        while (combined[pos] == '/') ++pos;
        if (component_length == 0U || text_equal(component, ".")) continue;
        if (text_equal(component, "..")) {
            if (depth != 0U) {
                length = component_starts[--depth];
            }
            continue;
        }
        if (depth >= (uint32_t)(sizeof(component_starts) / sizeof(component_starts[0]))) {
            return false;
        }
        component_starts[depth++] = length;
        if (length > 1U) {
            if (length + 1U >= capacity) return false;
            output[length++] = '/';
        }
        for (uint32_t i = 0U; i < component_length; ++i) {
            if (length + 1U >= capacity) return false;
            output[length++] = component[i];
        }
    }
    if (length >= capacity) return false;
    output[length] = '\0';
    return true;
}

static void command_mount(void)
{
    if (!vfs_is_mounted()) {
        console_write("No supported filesystem is mounted. Current driver: FAT32 read-only.\n");
        return;
    }
    console_write("/  <-  ");
    console_write(vfs_root_source());
    console_write(" (");
    console_write(vfs_root_filesystem());
    console_write(", read-only)\n");
}

static void command_list(const char *argument)
{
    char path[512];
    uint32_t count = 0U;
    if (!vfs_is_mounted()) {
        console_write("No mounted filesystem. Connect a FAT32 volume and reboot/rescan.\n");
        return;
    }
    if (!normalize_path(argument, path, sizeof(path)) ||
        !vfs_list(path, directory_entries, VFS_MAX_DIR_ENTRIES, &count)) {
        console_write("ls: cannot open directory: ");
        console_write(argument != 0 && *argument != '\0' ? argument : current_directory);
        console_write("\n");
        return;
    }
    for (uint32_t i = 0U; i < count; ++i) {
        if (directory_entries[i].is_directory) console_set_color(0x0b);
        console_write(directory_entries[i].name);
        console_set_color(0x07);
        if (directory_entries[i].is_directory) {
            console_write("/\n");
        } else {
            console_write("  ");
            console_write_u64(directory_entries[i].size);
            console_write(" bytes\n");
        }
    }
    if (count == 0U) console_write("(empty directory)\n");
    if (count == VFS_MAX_DIR_ENTRIES) {
        console_write("[showing first 64 entries]\n");
    }
}

static void command_change_directory(const char *argument)
{
    char path[512];
    uint32_t ignored_count = 0U;
    if (!vfs_is_mounted() || !normalize_path(argument, path, sizeof(path)) ||
        !vfs_list(path, directory_entries, VFS_MAX_DIR_ENTRIES, &ignored_count)) {
        console_write("cd: directory not found\n");
        return;
    }
    uint32_t i = 0U;
    while (path[i] != '\0' && i + 1U < sizeof(current_directory)) {
        current_directory[i] = path[i];
        ++i;
    }
    current_directory[i] = '\0';
}

static void command_cat(const char *argument)
{
    char path[512];
    uint32_t bytes_read = 0U;
    uint32_t file_size = 0U;
    if (!vfs_is_mounted() || argument == 0 || *argument == '\0' ||
        !normalize_path(argument, path, sizeof(path))) {
        console_write("Usage: cat /path/to/file (FAT32 volume required)\n");
        return;
    }
    if (!vfs_read(path, 0U, file_output_buffer, 65536U,
                  &bytes_read, &file_size)) {
        console_write("cat: cannot read file: ");
        console_write(path);
        console_write("\n");
        return;
    }
    for (uint32_t i = 0U; i < bytes_read; ++i) {
        uint8_t c = (uint8_t)file_output_buffer[i];
        if (c == 0U || (c < 0x20U && c != '\n' && c != '\r' && c != '\t' && c != '\b')) {
            file_output_buffer[i] = '?';
        }
    }
    file_output_buffer[bytes_read] = '\0';
    console_write(file_output_buffer);
    if (bytes_read == 0U || file_output_buffer[bytes_read - 1U] != '\n') {
        console_write("\n");
    }
    if (file_size > bytes_read) {
        console_write("[truncated: displayed first 65536 of ");
        console_write_u64(file_size);
        console_write(" bytes]\n");
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

    /* Registered ELF commands take precedence over the legacy built-ins. */
    if (perwix_command_execute(command, argument)) {
        return;
    }

    if (text_equal(command, "help")) {
        print_boot_help();
    } else if (text_equal(command, "clear")) {
        console_clear();
    } else if (text_equal(command, "modules") || text_equal(command, "lsmod")) {
        console_write("Loaded ELF modules: ");
        console_write_u64(perwix_module_count());
        console_write("\n");
        for (uint32_t i = 0; i < perwix_module_count(); ++i) {
            console_write("  ");
            console_write(perwix_module_name(i));
            console_write("\n");
        }
        perwix_command_list();
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
        uint32_t partitions = 0U;
        for (uint32_t d = 0; d < storage_disk_count(); ++d) {
            partitions += storage_get_partition_count(d);
        }
        console_write("Detected formats: FAT12/16/32, exFAT, NTFS, ext2/3/4, ISO9660, ");
        console_write("Btrfs, XFS, F2FS, HFS+, APFS, UFS, ReiserFS, SquashFS, JFS, LUKS, LVM2.\n");
        console_write("ATA disks: ");
        console_write_u64(storage_disk_count());
        console_write("; partitions: ");
        console_write_u64(partitions);
        console_write("; VFS drivers: ");
        console_write_u64(vfs_registered_count());
        console_write("; root mount: ");
        console_write(vfs_is_mounted() ? vfs_root_filesystem() : "none");
        console_write("\nOnly FAT32 currently has mounted read-only file access; other formats are signature-detected.\n");
    } else if (text_equal(command, "mount")) {
        command_mount();
    } else if (text_equal(command, "pwd")) {
        console_write(current_directory);
        console_write("\n");
    } else if (text_equal(command, "ls")) {
        command_list(argument);
    } else if (text_equal(command, "cd")) {
        command_change_directory(argument);
    } else if (text_equal(command, "cat")) {
        command_cat(argument);
    } else if (text_equal(command, "layout")) {
        console_write("Keyboard layout: ");
        console_write(keyboard_layout_name());
        console_write(" (Alt+Shift toggles US/UA).\n");
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
    uint32_t length = 0U;
    for (;;) {
        uint32_t codepoint = 0U;
        uint8_t key = keyboard_read_codepoint(&codepoint);
        if (key != PERWIX_KEY_CHAR) {
            mouse_poll();
            __asm__ volatile("pause");
            continue;
        }
        if (codepoint == '\n') {
            console_write("\n");
            buffer[length] = '\0';
            return length;
        }
        if (codepoint == '\b') {
            if (length != 0U) {
                do {
                    --length;
                } while (length != 0U &&
                         (((uint8_t)buffer[length] & 0xc0U) == 0x80U));
                console_write("\b");
            }
            continue;
        }
        if (codepoint >= 0x20U && codepoint != 0x7fU) {
            char encoded[5];
            uint32_t count = utf8_encode_one(codepoint, encoded);
            if (count != 0U && length + count < capacity) {
                for (uint32_t i = 0U; i < count; ++i) buffer[length++] = encoded[i];
                encoded[count] = '\0';
                console_write(encoded);
            }
        }
    }
}

void shell_run(void)
{
    console_set_color(0x0b);
    console_write("Welcome to Perwix. Type help to see available commands.\n");
    console_set_color(0x07);

    char line[512];
    for (;;) {
        console_set_color(0x0a);
        console_write("perwix");
        console_set_color(0x07);
        console_write("@pc:");
        console_write(current_directory);
        console_write(" $ ");
        (void)read_line(line, sizeof(line));
        run_command(line);
    }
}

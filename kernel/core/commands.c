/*
 * Extra shell commands: neofetch, networking tools, system utilities.
 * shell.c calls extra_command_run() for anything it does not know itself.
 */
#include "perwix.h"
#include "../drivers/framebuffer.h"
#include "netdev.h"
#include "vfs.h"
#include "driver.h"

#define COLOR_DEFAULT 0x07
#define COLOR_CYAN 0x0b
#define COLOR_GREEN 0x0a
#define COLOR_RED 0x0c
#define COLOR_YELLOW 0x0e
#define COLOR_GRAY 0x08

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static bool same(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) {
            return false;
        }
    }
    return *left == *right;
}

static void say(uint8_t color, const char *text)
{
    console_set_color(color);
    console_write(text);
    console_set_color(COLOR_DEFAULT);
}

static void write_padded_u64(uint64_t value, uint32_t width)
{
    uint32_t digits = 1;
    for (uint64_t v = value; v >= 10; v /= 10) {
        ++digits;
    }
    for (uint32_t i = digits; i < width; ++i) {
        console_write("0");
    }
    console_write_u64(value);
}

static void write_i64(int64_t value)
{
    if (value < 0) {
        console_write("-");
        console_write_u64((uint64_t)(-(value + 1)) + 1U);
    } else {
        console_write_u64((uint64_t)value);
    }
}

/* Split off the first word of `text` (in place); returns the rest. */
static char *next_word(char *text, char **word)
{
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    *word = text;
    while (*text != '\0' && *text != ' ' && *text != '\t') {
        ++text;
    }
    if (*text != '\0') {
        *text++ = '\0';
        while (*text == ' ' || *text == '\t') {
            ++text;
        }
    }
    return text;
}

static bool parse_i64(const char *text, int64_t *value)
{
    bool negative = false;
    if (*text == '-') {
        negative = true;
        ++text;
    }
    if (*text < '0' || *text > '9') {
        return false;
    }
    int64_t result = 0;
    while (*text >= '0' && *text <= '9') {
        result = result * 10 + (*text - '0');
        if (result > 1000000000000000000LL) {
            return false;
        }
        ++text;
    }
    if (*text != '\0') {
        return false;
    }
    *value = negative ? -result : result;
    return true;
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c,
                  uint32_t *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0));
}

/* 48-byte processor brand string (CPUID 0x80000002..4), trimmed. */
static const char *cpu_brand(void)
{
    static char brand[49];
    uint32_t a, b, c, d;
    cpuid(0x80000000U, &a, &b, &c, &d);
    if (a < 0x80000004U) {
        return "Unknown x86-64 CPU";
    }
    for (uint32_t leaf = 0; leaf < 3; ++leaf) {
        cpuid(0x80000002U + leaf, &a, &b, &c, &d);
        uint32_t regs[4] = { a, b, c, d };
        for (uint32_t r = 0; r < 4; ++r) {
            for (uint32_t byte = 0; byte < 4; ++byte) {
                brand[leaf * 16 + r * 4 + byte] = (char)(regs[r] >> (byte * 8));
            }
        }
    }
    brand[48] = '\0';
    const char *start = brand;
    while (*start == ' ') {
        ++start;
    }
    return start;
}

/* ------------------------------------------------------------------ */
/* neofetch                                                            */
/* ------------------------------------------------------------------ */

/* neofetch draws the boot logo (perwix_logo) scaled down to this grid. */
#define NEOFETCH_LOGO_WIDTH 20
#define NEOFETCH_LOGO_HEIGHT 9

static void neofetch_label(const char *label)
{
    say(COLOR_CYAN, label);
    console_write(": ");
}

static void neofetch_uptime(void)
{
    uint64_t seconds = system_ticks / PERWIX_TIMER_HZ;
    if (seconds >= 3600) {
        console_write_u64(seconds / 3600);
        console_write("h ");
    }
    console_write_u64((seconds / 60) % 60);
    console_write("m ");
    console_write_u64(seconds % 60);
    console_write("s");
}

static void neofetch_color_row(uint8_t first)
{
    for (uint8_t i = 0; i < 8; ++i) {
        console_set_color((uint8_t)(first + i));
        console_write("###");
    }
    console_set_color(COLOR_DEFAULT);
}

static void neofetch(void)
{
    console_write("\n");
    for (uint32_t row = 0; row < NEOFETCH_LOGO_HEIGHT + 2; ++row) {
        if (row < NEOFETCH_LOGO_HEIGHT) {
            console_write_logo_row(row, NEOFETCH_LOGO_WIDTH,
                                   NEOFETCH_LOGO_HEIGHT);
        } else {
            console_write("                    ");
        }
        console_write("  ");
        switch (row) {
        case 0:
            say(COLOR_GREEN, "root");
            console_write("@");
            say(COLOR_GREEN, "perwix");
            break;
        case 1:
            say(COLOR_GRAY, "-----------");
            break;
        case 2:
            neofetch_label("OS");
            console_write("Perwix 1.1 amd64");
            break;
        case 3:
            neofetch_label("Kernel");
            console_write("perwix.elf (Multiboot2, GRUB2)");
            break;
        case 4:
            neofetch_label("Uptime");
            neofetch_uptime();
            break;
        case 5:
            neofetch_label("Shell");
            console_write("perwix-sh");
            break;
        case 6:
            neofetch_label("Display");
            console_write("VGA text 80x25 + COM1 serial");
            break;
        case 7:
            neofetch_label("CPU");
            console_write(cpu_brand());
            break;
        case 8:
            neofetch_label("Memory");
            console_write_u64((pmm_bootstrap_pages() * 4) / 1024);
            console_write(" MiB free (bootstrap)");
            break;
        case 9:
            neofetch_label("PCI");
            console_write_u64(pci_device_count());
            console_write(" device(s), ");
            console_write_u64(usb_controller_count());
            console_write(" USB controller(s)");
            break;
        case 10:
            neofetch_label("Network");
            if (net_ready()) {
                console_write(net_device()->name);
                if (net_ip() != 0) {
                    console_write(" ");
                    console_write_ipv4(net_ip());
                } else {
                    console_write(" (run dhcp)");
                }
            } else {
                console_write("no supported adapter");
            }
            break;
        case 11:
            neofetch_label("Drivers");
            console_write_u64(driver_registered_count());
            console_write(" registered");
            break;
        case 12:
            neofetch_color_row(1);
            break;
        case 13:
            neofetch_color_row(9);
            break;
        default:
            break;
        }
        console_write("\n");
    }
    console_write("\n");
}

/* ------------------------------------------------------------------ */
/* Networking commands                                                 */
/* ------------------------------------------------------------------ */

static bool require_nic(void)
{
    if (!net_ready()) {
        say(COLOR_RED, "No supported network adapter is active.\n");
        return false;
    }
    return true;
}

static void cmd_ifconfig(void)
{
    if (!require_nic()) {
        return;
    }
    const netdev_t *dev = net_device();
    say(COLOR_CYAN, dev->name);
    console_write(dev->link_up(( netdev_t *)dev) ? "  link UP\n" : "  link DOWN\n");
    console_write("  MAC      ");
    console_write_mac(dev->mac);
    console_write("\n  IPv4     ");
    if (net_ip() == 0) {
        console_write("not configured (use dhcp)\n");
    } else {
        console_write_ipv4(net_ip());
        console_write("  netmask ");
        console_write_ipv4(net_netmask());
        console_write("\n  gateway  ");
        console_write_ipv4(net_gateway());
        console_write("   dns ");
        console_write_ipv4(net_dns_server());
        console_write(net_dhcp_used() ? "\n  source   DHCP\n"
                                      : "\n  source   static QEMU defaults\n");
    }
    console_write("  frames   rx ");
    console_write_u64(net_received_frames());
    console_write(", tx ");
    console_write_u64(net_sent_frames());
    console_write("\n");
}

static void cmd_net(void)
{
    uint32_t found = 0;
    for (uint32_t i = 0; i < pci_device_count(); ++i) {
        uint8_t bus, device, function, class_code, subclass, interface;
        uint16_t vendor_id, device_id;
        if (!pci_device_get(i, &bus, &device, &function, &vendor_id,
                            &device_id, &class_code, &subclass, &interface) ||
            class_code != 0x02) {
            continue;
        }
        ++found;
        console_write("PCI network adapter ");
        console_write_hex(vendor_id);
        console_write(":");
        console_write_hex(device_id);
        console_write("\n");
    }
    if (found == 0) {
        console_write("No PCI network adapter found.\n");
    }
    if (net_ready()) {
        cmd_ifconfig();
        console_write("Stack: ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP (no TLS).\n");
    } else {
        console_write("No supported network driver is active.\n");
        console_write("Supported: e1000, rtl8139, rtl8169, virtio-net, pcnet32, ne2k-pci.\n");
    }
}

static void cmd_dhcp(void)
{
    if (!require_nic()) {
        return;
    }
    console_write("Requesting a lease via DHCP... ");
    if (net_dhcp()) {
        say(COLOR_GREEN, "bound ");
        console_write_ipv4(net_ip());
        console_write("\n");
    } else {
        say(COLOR_RED, "no answer\n");
    }
}

static void cmd_ping(char *argument)
{
    char *host;
    next_word(argument, &host);
    if (*host == '\0') {
        console_write("Usage: ping HOST|A.B.C.D\n");
        return;
    }
    if (!require_nic()) {
        return;
    }
    uint32_t address;
    if (!net_resolve(host, &address)) {
        say(COLOR_RED, "Cannot resolve ");
        console_write(host);
        console_write("\n");
        return;
    }
    console_write("PING ");
    console_write(host);
    console_write(" (");
    console_write_ipv4(address);
    console_write(")\n");
    uint32_t replies = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t rtt = 0;
        if (net_ping_time(address, &rtt)) {
            ++replies;
            say(COLOR_GREEN, "reply");
            console_write(" from ");
            console_write_ipv4(address);
            console_write(": seq=");
            console_write_u64(i + 1);
            console_write(rtt == 0 ? " time<10 ms\n" : " time~");
            if (rtt != 0) {
                console_write_u64(rtt);
                console_write(" ms\n");
            }
        } else {
            say(COLOR_RED, "timeout");
            console_write(" seq=");
            console_write_u64(i + 1);
            console_write("\n");
        }
    }
    console_write_u64(replies);
    console_write("/4 replies\n");
}

static void cmd_nslookup(char *argument)
{
    char *host;
    next_word(argument, &host);
    if (*host == '\0') {
        console_write("Usage: nslookup HOST\n");
        return;
    }
    if (!require_nic()) {
        return;
    }
    uint32_t address;
    if (net_resolve(host, &address)) {
        console_write(host);
        console_write(" -> ");
        say(COLOR_GREEN, "");
        console_write_ipv4(address);
        console_write("  (via DNS ");
        console_write_ipv4(net_dns_server());
        console_write(")\n");
    } else {
        say(COLOR_RED, "DNS lookup failed.\n");
    }
}

static void cmd_arp(void)
{
    uint32_t count = net_arp_entry_count();
    if (count == 0) {
        console_write("ARP cache is empty.\n");
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t ip;
        uint8_t mac[6];
        if (net_arp_entry_get(i, &ip, mac)) {
            console_write_ipv4(ip);
            console_write("  ");
            console_write_mac(mac);
            console_write("\n");
        }
    }
}

/* HTTP output: print text, drop CR and control bytes, cap the output. */
#define HTTP_SHOW_LIMIT 3000
static uint32_t http_shown;

static void http_sink(const char *data, uint32_t length)
{
    char text[2] = { 0, 0 };
    for (uint32_t i = 0; i < length && http_shown < HTTP_SHOW_LIMIT; ++i) {
        char c = data[i];
        if (c == '\n' || ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7f)) {
            text[0] = c;
            console_write(text);
        }
        ++http_shown;
    }
}

static void cmd_http(char *argument)
{
    char *url;
    next_word(argument, &url);
    if (*url == '\0') {
        console_write("Usage: http HOST[/path]   (plain HTTP only, no HTTPS)\n");
        return;
    }
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p') {
        if (url[4] == 's') {
            say(COLOR_RED, "HTTPS needs TLS, which Perwix does not have yet.\n");
            return;
        }
        if (url[4] == ':' && url[5] == '/' && url[6] == '/') {
            url += 7;
        }
    }
    static char host[64];
    static char path[128];
    uint32_t h = 0;
    while (*url != '\0' && *url != '/' && h < sizeof(host) - 1) {
        host[h++] = *url++;
    }
    host[h] = '\0';
    uint32_t p = 0;
    if (*url == '\0') {
        path[p++] = '/';
    }
    while (*url != '\0' && p < sizeof(path) - 1) {
        path[p++] = *url++;
    }
    path[p] = '\0';
    if (h == 0 || !require_nic()) {
        return;
    }

    console_write("GET http://");
    console_write(host);
    console_write(path);
    console_write("\n");
    http_shown = 0;
    int result = net_http_get(host, path, http_sink);
    console_write("\n");
    if (result == -1) {
        say(COLOR_RED, "DNS lookup failed.\n");
    } else if (result == -2) {
        say(COLOR_RED, "Could not connect (port 80).\n");
    } else if (result < 0) {
        say(COLOR_RED, "Request was not delivered.\n");
    } else {
        say(COLOR_GREEN, "[done] ");
        console_write_u64((uint64_t)result);
        console_write(" bytes received");
        if (http_shown >= HTTP_SHOW_LIMIT) {
            console_write(" (display truncated)");
        }
        console_write("\n");
    }
}

/* ------------------------------------------------------------------ */
/* System utilities                                                    */
/* ------------------------------------------------------------------ */

static uint8_t cmos_read(uint8_t index)
{
    outb(0x70, index);
    return inb(0x71);
}

static uint8_t bcd_to_binary(uint8_t value)
{
    return (uint8_t)((value & 0x0f) + ((value >> 4) * 10));
}

static void cmd_date(void)
{
    for (uint32_t w = 0; w < 100000 && (cmos_read(0x0a) & 0x80U) != 0; ++w) {
    }
    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t status = cmos_read(0x0b);
    bool pm = (hour & 0x80U) != 0;
    if ((status & 4U) == 0) {                      /* BCD mode */
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour & 0x7f);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    } else {
        hour &= 0x7f;
    }
    if ((status & 2U) == 0 && pm) {                /* 12-hour clock */
        hour = (uint8_t)((hour % 12) + 12);
    }
    write_padded_u64(2000U + year, 4);
    console_write("-");
    write_padded_u64(month, 2);
    console_write("-");
    write_padded_u64(day, 2);
    console_write(" ");
    write_padded_u64(hour, 2);
    console_write(":");
    write_padded_u64(minute, 2);
    console_write(":");
    write_padded_u64(second, 2);
    console_write("  (hardware clock)\n");
}

static void cmd_cpuinfo(void)
{
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    console_write("Model    ");
    console_write(cpu_brand());
    console_write("\nFamily   ");
    console_write_u64(((a >> 8) & 0xf) + (((a >> 8) & 0xf) == 0xf ? (a >> 20) & 0xff : 0));
    console_write("  model ");
    console_write_u64(((a >> 4) & 0xf) | (((a >> 16) & 0xf) << 4));
    console_write("  stepping ");
    console_write_u64(a & 0xf);
    console_write("\nFeatures");
    static const struct { bool ecx; uint8_t bit; const char *name; } features[] = {
        { false, 0, "fpu" }, { false, 4, "tsc" }, { false, 6, "pae" },
        { false, 9, "apic" }, { false, 23, "mmx" }, { false, 25, "sse" },
        { false, 26, "sse2" }, { true, 0, "sse3" }, { true, 9, "ssse3" },
        { true, 19, "sse4.1" }, { true, 20, "sse4.2" }, { true, 28, "avx" },
        { true, 31, "hypervisor" },
    };
    for (uint32_t i = 0; i < sizeof(features) / sizeof(features[0]); ++i) {
        uint32_t word = features[i].ecx ? c : d;
        if ((word >> features[i].bit) & 1U) {
            console_write(" ");
            console_write(features[i].name);
        }
    }
    console_write("\n");
}

static void cmd_colors(void)
{
    for (uint8_t i = 1; i < 16; ++i) {
        console_set_color(i);
        console_write("Perwix ");
        if (i == 7) {
            console_write("\n");
        }
    }
    console_set_color(COLOR_DEFAULT);
    console_write("\n");
}

static void cmd_calc(char *argument)
{
    char *left;
    char *op;
    char *right;
    char *rest = next_word(argument, &left);
    rest = next_word(rest, &op);
    (void)next_word(rest, &right);
    int64_t a, b;
    if (!parse_i64(left, &a) || !parse_i64(right, &b) || op[1] != '\0') {
        console_write("Usage: calc A OP B   (OP is + - * / %)\n");
        return;
    }
    int64_t result = 0;
    switch (op[0]) {
    case '+': result = a + b; break;
    case '-': result = a - b; break;
    case '*': result = a * b; break;
    case '/':
    case '%':
        if (b == 0) {
            say(COLOR_RED, "Division by zero.\n");
            return;
        }
        result = op[0] == '/' ? a / b : a % b;
        break;
    default:
        console_write("Usage: calc A OP B   (OP is + - * / %)\n");
        return;
    }
    write_i64(result);
    console_write("\n");
}

static void cmd_sleep(char *argument)
{
    char *word;
    next_word(argument, &word);
    int64_t seconds;
    if (!parse_i64(word, &seconds) || seconds < 0 || seconds > 3600) {
        console_write("Usage: sleep SECONDS   (0-3600)\n");
        return;
    }
    uint64_t end = system_ticks + (uint64_t)seconds * PERWIX_TIMER_HZ;
    while (system_ticks < end) {
        __asm__ volatile("hlt");
    }
}

static void cmd_poweroff(void)
{
    console_write("Powering off...\n");
    outw(0x604, 0x2000);        /* QEMU q35 / ACPI PM */
    outw(0xb004, 0x2000);       /* QEMU i440fx, Bochs */
    outw(0x4004, 0x3400);       /* VirtualBox */
    console_write("This machine does not support software power-off; halted.\n");
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}


/* ------------------------------------------------------------------ */
/* Perwix 1.1 additions                                                 */
/* ------------------------------------------------------------------ */

static void cmd_sysinfo(void)
{
    console_write("Perwix 1.1 / x86-64 kernel\n");
    console_write("CPU: ");
    console_write(cpu_brand());
    console_write("\nUptime: ");
    console_write_u64(system_ticks / PERWIX_TIMER_HZ);
    console_write(" seconds\nMemory: ");
    console_write_u64((pmm_bootstrap_pages() * 4) / 1024);
    console_write(" MiB free in bootstrap allocator\nPCI devices: ");
    console_write_u64(pci_device_count());
    console_write("; USB controllers: ");
    console_write_u64(usb_controller_count());
    console_write("; network: ");
    console_write(net_ready() ? "initialized" : "not initialized");
    console_write("\n");
}

static void cmd_meminfo(void)
{
    uint64_t pages = pmm_bootstrap_pages();
    console_write("Bootstrap physical allocator\nFree pages: ");
    console_write_u64(pages);
    console_write(" x 4096 bytes\nApproximate free: ");
    console_write_u64(pages * 4);
    console_write(" KiB (");
    console_write_u64((pages * 4) / 1024);
    console_write(" MiB)\nNote: this is allocator availability, not total installed RAM.\n");
}

static void cmd_tickinfo(void)
{
    console_write("PIT frequency: ");
    console_write_u64(PERWIX_TIMER_HZ);
    console_write(" Hz\nKernel ticks: ");
    console_write_u64(system_ticks);
    console_write("\nElapsed: ");
    console_write_u64(system_ticks / PERWIX_TIMER_HZ);
    console_write(" seconds\n");
}

static void print_usb_family(const char *label, uint8_t interface)
{
    console_write(label);
    console_write(": ");
    console_write_u64(usb_controller_family_count(interface));
    console_write(" detected, ");
    console_write_u64(usb_controller_family_ready_count(interface));
    console_write(" reset OK\n");
}

static void cmd_usbinfo(void)
{
    console_write("USB host-controller support\n");
    print_usb_family("USB 1.0/1.1 UHCI", 0x00);
    print_usb_family("USB 1.0/1.1 OHCI", 0x10);
    print_usb_family("USB 2.0 EHCI", 0x20);
    print_usb_family("USB 2.0/3.x xHCI", 0x30);
    console_write("USB 3.1/3.2 use xHCI when provided by the platform.\n");
    console_write("USB4 uses tunneling/router infrastructure, not a separate xHCI generation; USB4 router support is not implemented.\n");
    console_write("Total controllers: ");
    console_write_u64(usb_controller_count());
    console_write("; reset OK: ");
    console_write_u64(usb_controller_ready_count());
    console_write("; xHCI connected ports: ");
    console_write_u64(usb_connected_port_count());
    console_write("\nTransfers, enumeration, mass storage and USB HID are not implemented yet.\n");
}

static void cmd_netstat(void)
{
    if (!net_ready()) {
        console_write("No initialized network interface.\n");
        return;
    }
    const netdev_t *dev = net_device();
    console_write("Interface: ");
    console_write(dev != 0 && dev->name != 0 ? dev->name : "unknown");
    console_write("\nLink: ");
    console_write(dev != 0 && dev->link_up != 0 && dev->link_up((netdev_t *)dev)
                      ? "up" : "down/unknown");
    console_write("\nMAC: ");
    if (dev != 0) console_write_mac(dev->mac);
    console_write("\nIPv4: ");
    console_write_ipv4(net_ip());
    console_write("\nNetmask: ");
    console_write_ipv4(net_netmask());
    console_write("\nGateway: ");
    console_write_ipv4(net_gateway());
    console_write("\nTX frames: ");
    console_write_u64(net_sent_frames());
    console_write("\nRX frames: ");
    console_write_u64(net_received_frames());
    console_write("\nARP entries: ");
    console_write_u64(net_arp_entry_count());
    console_write("\n");
}

static uint32_t random_state = 0x6d2b79f5U;

static uint32_t random_next(void)
{
    uint32_t value = random_state ^ (uint32_t)system_ticks;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value == 0 ? 0x6d2b79f5U : value;
    return random_state;
}

static void cmd_rand(char *argument)
{
    char *count_text;
    char *rest = next_word(argument, &count_text);
    int64_t count = 1;
    if (*count_text != '\0' && !parse_i64(count_text, &count)) {
        count = 0;
    }
    if (*rest != '\0' || count < 1 || count > 16) {
        console_write("Usage: rand [COUNT]  (COUNT: 1-16; not cryptographic)\n");
        return;
    }
    for (int64_t i = 0; i < count; ++i) {
        console_write_u64(random_next());
        console_write("\n");
    }
    console_write("Note: rand is a simple pseudo-random helper, not secure entropy.\n");
}

static void cmd_beep(char *argument)
{
    char *frequency_text;
    char *rest = next_word(argument, &frequency_text);
    int64_t frequency = 880;
    if (*frequency_text != '\0' && !parse_i64(frequency_text, &frequency)) {
        frequency = 0;
    }
    if (*rest != '\0' || frequency < 200 || frequency > 4000) {
        console_write("Usage: beep [FREQUENCY_HZ]  (200-4000; default 880)\n");
        return;
    }

    uint32_t divisor = 1193182U / (uint32_t)frequency;
    outb(0x43, 0xb6);
    outb(0x42, (uint8_t)divisor);
    outb(0x42, (uint8_t)(divisor >> 8));
    uint8_t speaker = inb(0x61);
    outb(0x61, (uint8_t)(speaker | 0x03U));
    uint64_t end_tick = system_ticks + 12;
    while (system_ticks < end_tick) {
        __asm__ volatile("hlt");
    }
    outb(0x61, (uint8_t)(inb(0x61) & (uint8_t)~0x03U));
    console_write("PC speaker tone completed.\n");
}

static void cmd_pciinfo(void)
{
    uint32_t storage = 0, network = 0, display = 0, bridge = 0;
    uint32_t usb = 0, other = 0;
    for (uint32_t i = 0; i < pci_device_count(); ++i) {
        uint8_t bus, device, function, class_code, subclass, interface;
        uint16_t vendor, product;
        if (!pci_device_get(i, &bus, &device, &function, &vendor, &product,
                            &class_code, &subclass, &interface)) {
            continue;
        }
        (void)bus; (void)device; (void)function;
        (void)subclass; (void)interface; (void)vendor; (void)product;
        switch (class_code) {
        case 0x01: ++storage; break;
        case 0x02: ++network; break;
        case 0x03: ++display; break;
        case 0x06: ++bridge; break;
        case 0x0c: ++usb; break;
        default: ++other; break;
        }
    }
    console_write("PCI inventory (class counts)\nTotal: ");
    console_write_u64(pci_device_count());
    console_write("\nStorage: "); console_write_u64(storage);
    console_write("\nNetwork: "); console_write_u64(network);
    console_write("\nDisplay: "); console_write_u64(display);
    console_write("\nBridges: "); console_write_u64(bridge);
    console_write("\nSerial bus / USB class: "); console_write_u64(usb);
    console_write("\nOther: "); console_write_u64(other);
    console_write("\nUse `pci` for individual device IDs.\n");
}

static void cmd_drivercheck(void)
{
    console_write("Perwix driver status\n");
    console_write("PCI devices discovered: ");
    console_write_u64(pci_device_count());
    console_write("\nUSB controllers initialized: ");
    console_write_u64(usb_controller_ready_count());
    console_write(" / ");
    console_write_u64(usb_controller_count());
    console_write("\nNetwork stack: ");
    console_write(net_ready() ? "ready" : "no NIC initialized");
    console_write("\nRegistered driver descriptors: ");
    console_write_u64(driver_registered_count());
    console_write("\nUSB packet transfers/HID and disk drivers: pending.\n");
}

static void cmd_logo(void)
{
    console_clear();
    console_draw_logo();
    console_write("Perwix 1.1 - x86-64 kernel\nType `help` for commands.\n");
}


/* Display, UTF-8 and low-level diagnostics. These commands deliberately
 * report the capabilities of the current VGA/serial backend without
 * pretending that legacy VGA text mode can draw arbitrary Unicode glyphs. */
static void cmd_uptimefmt(void)
{
    uint64_t seconds = system_ticks / PERWIX_TIMER_HZ;
    uint64_t days = seconds / 86400U;
    uint64_t hours = (seconds / 3600U) % 24U;
    uint64_t minutes = (seconds / 60U) % 60U;
    seconds %= 60U;
    console_write("Uptime: ");
    console_write_u64(days); console_write("d ");
    write_padded_u64(hours, 2); console_write(":");
    write_padded_u64(minutes, 2); console_write(":");
    write_padded_u64(seconds, 2); console_write("\n");
}

static void cmd_displayinfo(void)
{
    if (framebuffer_is_active()) {
        console_write("Display backend: Multiboot2 RGB framebuffer\n");
        console_write("Resolution: ");
        console_write_u64(framebuffer_width());
        console_write("x");
        console_write_u64(framebuffer_height());
        console_write("x");
        console_write_u64(framebuffer_bits_per_pixel());
        console_write(" bpp\n");
        console_write("GPU output: firmware/GRUB-configured mode; HDMI works when the GPU routes this framebuffer to its HDMI connector.\n");
        console_write("Text: native 8x16 bitmap pixels, dynamically sized grid; no font interpolation\n");
    } else {
        console_write("Display backend: VGA text 80x25 + COM1 serial\n");
        console_write("No supported Multiboot2 RGB framebuffer was supplied by GRUB.\n");
    }
    console_write("Serial: UTF-8 bytes passed through unchanged\n");
    console_write("Framebuffer pixels are written directly; HDMI link training, hot-plug, EDID and GPU modesetting remain firmware/GPU responsibilities.\n");
}

static void cmd_utf8test(void)
{
    console_write("UTF-8 test: Україна | Привіт | українська абетка | Ґґ Єє Іі Її\n");
    console_write("UTF-8 decoding accepts valid Unicode scalar values; unsupported glyphs use '?'.\n");
    console_write("Keyboard: Alt+Shift toggles between US and Ukrainian layouts.\n");
    console_write("VGA text fallback is ASCII-only; HDMI framebuffer uses the embedded Cyrillic bitmap font.\n");
}

static void cmd_usbports(void)
{
    console_write("USB host-controller summary\nControllers found: ");
    console_write_u64(usb_controller_count());
    console_write("\nControllers reset successfully: ");
    console_write_u64(usb_controller_ready_count());
    console_write("\nConnected root-hub ports reported by xHCI: ");
    console_write_u64(usb_connected_port_count());
    console_write("\nUSB device enumeration and storage/HID transfers remain pending.\n");
}

static void cmd_tickrate(void)
{
    console_write("Timer source: legacy PIT\nConfigured tick rate: ");
    console_write_u64(PERWIX_TIMER_HZ);
    console_write(" Hz\nTicks since boot: ");
    console_write_u64(system_ticks);
    console_write("\n");
}

static void cmd_pageinfo(void)
{
    uint64_t pages = pmm_bootstrap_pages();
    console_write("Bootstrap page allocator\nPage size: 4096 bytes\nAvailable pages: ");
    console_write_u64(pages);
    console_write("\nApproximate available memory: ");
    console_write_u64(pages * 4U);
    console_write(" KiB\nThis is allocator state, not total installed RAM.\n");
}

static void cmd_kversion(void)
{
    console_write("Perwix kernel 1.1\nArchitecture: x86-64\nBoot: Multiboot2 / GRUB2\n");
    console_write("Console: VGA text + COM1 serial\n");
}

static void cmd_drvcount(void)
{
    console_write("Registered driver descriptors: ");
    console_write_u64(driver_registered_count());
    console_write("\nPCI devices discovered: ");
    console_write_u64(pci_device_count());
    console_write("\nNetwork initialized: ");
    console_write(net_ready() ? "yes\n" : "no\n");
    console_write("USB controllers initialized: ");
    console_write_u64(usb_controller_ready_count());
    console_write("\n");
}

static void cmd_cpuidmax(void)
{
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    console_write("CPUID maximum basic leaf: ");
    console_write_u64(a);
    console_write("\nVendor: ");
    char vendor[13];
    vendor[0]=(char)b; vendor[1]=(char)(b>>8); vendor[2]=(char)(b>>16); vendor[3]=(char)(b>>24);
    vendor[4]=(char)d; vendor[5]=(char)(d>>8); vendor[6]=(char)(d>>16); vendor[7]=(char)(d>>24);
    vendor[8]=(char)c; vendor[9]=(char)(c>>8); vendor[10]=(char)(c>>16); vendor[11]=(char)(c>>24); vendor[12]='\0';
    console_write(vendor); console_write("\n");
}

static void cmd_hex(char *argument)
{
    char *word;
    next_word(argument, &word);
    int64_t value;
    if (!parse_i64(word, &value) || value < 0) {
        console_write("Usage: hex NONNEGATIVE_DECIMAL_NUMBER\n");
        return;
    }
    console_write("Decimal: "); console_write_u64((uint64_t)value);
    console_write("\nHex: "); console_write_hex((uint64_t)value);
    console_write("\n");
}

static void cmd_usbgen(void)
{
    console_write("USB controller families\n");
    console_write("UHCI/OHCI: USB 1.x controller families\n");
    console_write("EHCI: USB 2.0 high-speed controller family\n");
    console_write("xHCI: USB 1.x/2.0/3.x capable host-controller family\n");
    console_write("USB4/TB tunnelling requires additional PCI/USB4 drivers.\n");
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

void extra_help(void)
{
    say(COLOR_YELLOW, "System   ");
    console_write("sysinfo meminfo tickinfo cpuinfo date hostname colors\n");
    console_write("         calc A op B  sleep N  rand [COUNT]  beep [HZ]\n");
    console_write("         logo uptimefmt displayinfo utf8test tickrate pageinfo kversion\n");
    console_write("         drvcount cpuidmax hex NUMBER usbgen drivercheck\n");
    say(COLOR_YELLOW, "Hardware ");
    console_write("usbinfo usbports usbgen pciinfo netstat\n");
    say(COLOR_YELLOW, "Network  ");
    console_write("ifconfig dhcp arp ping HOST nslookup HOST\n");
    console_write("         http HOST[/path]   (aliases: ip, dig, wget, curl)\n");
}

bool extra_command_run(const char *command, char *argument)
{
    if (same(command, "uptimefmt")) {
        cmd_uptimefmt();
    } else if (same(command, "displayinfo")) {
        cmd_displayinfo();
    } else if (same(command, "utf8test")) {
        cmd_utf8test();
    } else if (same(command, "usbports")) {
        cmd_usbports();
    } else if (same(command, "tickrate")) {
        cmd_tickrate();
    } else if (same(command, "pageinfo")) {
        cmd_pageinfo();
    } else if (same(command, "kversion")) {
        cmd_kversion();
    } else if (same(command, "drvcount")) {
        cmd_drvcount();
    } else if (same(command, "cpuidmax")) {
        cmd_cpuidmax();
    } else if (same(command, "hex")) {
        cmd_hex(argument);
    } else if (same(command, "usbgen")) {
        cmd_usbgen();
    } else if (same(command, "sysinfo")) {
        cmd_sysinfo();
    } else if (same(command, "meminfo")) {
        cmd_meminfo();
    } else if (same(command, "tickinfo")) {
        cmd_tickinfo();
    } else if (same(command, "usbinfo")) {
        cmd_usbinfo();
    } else if (same(command, "netstat")) {
        cmd_netstat();
    } else if (same(command, "rand")) {
        cmd_rand(argument);
    } else if (same(command, "beep")) {
        cmd_beep(argument);
    } else if (same(command, "pciinfo")) {
        cmd_pciinfo();
    } else if (same(command, "drivercheck")) {
        cmd_drivercheck();
    } else if (same(command, "logo")) {
        cmd_logo();
    } else if (same(command, "neofetch") || same(command, "fetch")) {
        neofetch();
    } else if (same(command, "ifconfig") || same(command, "ip")) {
        cmd_ifconfig();
    } else if (same(command, "net") || same(command, "network")) {
        cmd_net();
    } else if (same(command, "dhcp")) {
        cmd_dhcp();
    } else if (same(command, "ping")) {
        cmd_ping(argument);
    } else if (same(command, "nslookup") || same(command, "dig") ||
               same(command, "host")) {
        cmd_nslookup(argument);
    } else if (same(command, "arp")) {
        cmd_arp();
    } else if (same(command, "http") || same(command, "wget") ||
               same(command, "curl")) {
        cmd_http(argument);
    } else if (same(command, "date")) {
        cmd_date();
    } else if (same(command, "cpuinfo")) {
        cmd_cpuinfo();
    } else if (same(command, "hostname")) {
        console_write("perwix\n");
    } else if (same(command, "colors")) {
        cmd_colors();
    } else if (same(command, "calc")) {
        cmd_calc(argument);
    } else if (same(command, "sleep")) {
        cmd_sleep(argument);
    } else if (same(command, "poweroff") || same(command, "shutdown") ||
               same(command, "halt")) {
        cmd_poweroff();
    } else if (same(command, "lspci")) {
        return false;               /* handled as an alias in shell.c */
    } else {
        return false;
    }
    return true;
}

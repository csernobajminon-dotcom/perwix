#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UART_BASE 0x09000000UL
#define UART_DR (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_FR (*(volatile uint32_t *)(UART_BASE + 0x18))
#define UART_IBRD (*(volatile uint32_t *)(UART_BASE + 0x24))
#define UART_FBRD (*(volatile uint32_t *)(UART_BASE + 0x28))
#define UART_LCRH (*(volatile uint32_t *)(UART_BASE + 0x2c))
#define UART_CR (*(volatile uint32_t *)(UART_BASE + 0x30))
#define UART_ICR (*(volatile uint32_t *)(UART_BASE + 0x44))

static void put_char(char c)
{
    if (c == '\n') {
        put_char('\r');
    }
    while ((UART_FR & (1U << 5)) != 0) {
        __asm__ volatile("yield");
    }
    UART_DR = (uint8_t)c;
}

static void write(const char *text)
{
    while (*text != '\0') {
        put_char(*text++);
    }
}

static bool equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a++ != *b++) {
            return false;
        }
    }
    return *a == *b;
}

static char get_char(void)
{
    while ((UART_FR & (1U << 4)) != 0) {
        __asm__ volatile("yield");
    }
    return (char)(UART_DR & 0xff);
}

static uint64_t uptime_seconds(void)
{
    uint64_t counter;
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(counter));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency == 0 ? 0 : counter / frequency;
}

static void print_u64(uint64_t value)
{
    char digits[21];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0) {
        put_char(digits[--count]);
    }
}

static void print_help(void)
{
    write("Perwix ARM64 commands:\n");
    write("help       list commands\n");
    write("about      system overview\n");
    write("arch       processor architecture\n");
    write("version    kernel version\n");
    write("uptime     seconds since boot\n");
    write("drivers    active and pending device support\n");
    write("echo TEXT  print text\n");
    write("clear      clear serial terminal\n");
}

static void run_command(char *line)
{
    char *argument = line;
    while (*argument != '\0' && *argument != ' ') {
        ++argument;
    }
    if (*argument == ' ') {
        *argument++ = '\0';
        while (*argument == ' ') {
            ++argument;
        }
    }

    if (equal(line, "help")) {
        print_help();
    } else if (equal(line, "about")) {
        write("Perwix ARM64 kernel for QEMU virt / AArch64 PCs.\n");
        write("This build currently provides a PL011 serial shell.\n");
    } else if (equal(line, "arch")) {
        write("AArch64 (ARMv8-A), running at EL1.\n");
    } else if (equal(line, "version")) {
        write("Perwix 1.0 ARM64 preview\n");
    } else if (equal(line, "uptime")) {
        write("Uptime: ");
        print_u64(uptime_seconds());
        write(" seconds\n");
    } else if (equal(line, "drivers")) {
        write("[ready] PL011 UART console\n");
        write("[planned] GIC, timer, PCIe, USB and storage drivers\n");
    } else if (equal(line, "clear")) {
        write("\033[2J\033[H");
    } else if (equal(line, "echo")) {
        write(argument);
        write("\n");
    } else if (*line != '\0') {
        write("Unknown command. Type help.\n");
    }
}

void arm64_main(void)
{
    UART_CR = 0;
    UART_ICR = 0x7ff;
    UART_IBRD = 13;
    UART_FBRD = 1;
    UART_LCRH = 0x70;
    UART_CR = 0x301;

    write("\033[2J\033[H");
    write("Perwix 1.0 - AArch64 / ARMv8-A\n");
    write("[  OK  ] QEMU virt booted at EL1\n");
    write("[  OK  ] PL011 serial console ready\n");
    write("[ INFO ] ARM64 kernel build is separate from the x86-64 build\n");
    write("Type help for commands.\n");

    char line[128];
    for (;;) {
        write("perwix-arm64:/ $ ");
        uint32_t length = 0;
        for (;;) {
            char c = get_char();
            if (c == '\r' || c == '\n') {
                put_char('\n');
                line[length] = '\0';
                break;
            }
            if (c == '\b' || c == 0x7f) {
                if (length != 0) {
                    --length;
                    write("\b \b");
                }
                continue;
            }
            if ((uint8_t)c >= 0x20 && length + 1 < sizeof(line)) {
                line[length++] = c;
                put_char(c);
            }
        }
        run_command(line);
    }
}

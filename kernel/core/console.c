#include "perwix.h"

#define COM1 0x3f8
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_DEFAULT_COLOR 0x0f
#define LOGO_WIDTH 24
#define LOGO_HEIGHT 10
#define SCROLLBACK_LINES 256

static volatile uint16_t *const vga = (volatile uint16_t *)0xb8000;
static uint32_t cursor_x;
static uint32_t cursor_y;
static uint8_t console_color = VGA_DEFAULT_COLOR;
static uint16_t scrollback[SCROLLBACK_LINES][VGA_WIDTH];
static uint32_t scrollback_head;
static uint32_t scrollback_count;
static uint32_t scrollback_offset;

extern const char perwix_logo[][101];
extern const uint32_t perwix_logo_rows;
extern const uint32_t perwix_logo_width;

static void serial_putc(char c)
{
    for (uint32_t tries = 0; tries < 10000; ++tries) {
        if ((inb(COM1 + 5) & 0x20) != 0) {
            outb(COM1, (uint8_t)c);
            return;
        }
    }
}

static void scroll(void)
{
    for (uint32_t x = 0; x < VGA_WIDTH; ++x) {
        scrollback[scrollback_head][x] = vga[x];
    }
    scrollback_head = (scrollback_head + 1) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        ++scrollback_count;
    }
    scrollback_offset = 0;

    for (uint32_t y = 1; y < VGA_HEIGHT; ++y) {
        for (uint32_t x = 0; x < VGA_WIDTH; ++x) {
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
        }
    }
    for (uint32_t x = 0; x < VGA_WIDTH; ++x) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            (uint16_t)(VGA_DEFAULT_COLOR << 8) | ' ';
    }
    cursor_y = VGA_HEIGHT - 1;
}

static void vga_putc(char c)
{
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    scrollback_offset = 0;
    if (c == '\n') {
        cursor_x = 0;
        ++cursor_y;
    } else if (c == '\b') {
        if (cursor_x != 0) {
            --cursor_x;
            vga[cursor_y * VGA_WIDTH + cursor_x] =
                (uint16_t)(console_color << 8) | ' ';
        }
    } else {
        vga[cursor_y * VGA_WIDTH + cursor_x] =
            (uint16_t)(console_color << 8) | (uint8_t)c;
        ++cursor_x;
        if (cursor_x == VGA_WIDTH) {
            cursor_x = 0;
            ++cursor_y;
        }
    }
    if (cursor_y >= VGA_HEIGHT) {
        scroll();
    }
}

void console_clear(void)
{
    uint64_t flags = irq_save();
    cursor_x = 0;
    cursor_y = 0;
    scrollback_head = 0;
    scrollback_count = 0;
    scrollback_offset = 0;
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga[i] = (uint16_t)(VGA_DEFAULT_COLOR << 8) | ' ';
    }
    irq_restore(flags);
}

uint32_t console_scrollback_lines(void)
{
    return scrollback_count;
}

void console_scroll_view(int32_t lines)
{
    uint64_t flags = irq_save();
    int32_t next = (int32_t)scrollback_offset + lines;
    if (next < 0) {
        next = 0;
    }
    if ((uint32_t)next > scrollback_count) {
        next = (int32_t)scrollback_count;
    }
    scrollback_offset = (uint32_t)next;

    int32_t end = (int32_t)(scrollback_count + VGA_HEIGHT - 1) - next;
    int32_t start = end - (VGA_HEIGHT - 1);
    uint32_t oldest = (scrollback_head + SCROLLBACK_LINES -
                       scrollback_count) % SCROLLBACK_LINES;
    for (uint32_t y = 0; y < VGA_HEIGHT; ++y) {
        int32_t row = start + (int32_t)y;
        for (uint32_t x = 0; x < VGA_WIDTH; ++x) {
            uint16_t cell = (uint16_t)(VGA_DEFAULT_COLOR << 8) | ' ';
            if (row >= 0 && (uint32_t)row < scrollback_count) {
                uint32_t index = (oldest + (uint32_t)row) % SCROLLBACK_LINES;
                cell = scrollback[index][x];
            } else if (row >= (int32_t)scrollback_count &&
                       row < (int32_t)(scrollback_count + VGA_HEIGHT)) {
                uint32_t live_row = (uint32_t)(row - (int32_t)scrollback_count);
                cell = vga[live_row * VGA_WIDTH + x];
            }
            vga[y * VGA_WIDTH + x] = cell;
        }
    }
    irq_restore(flags);
}

void console_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);

    cursor_x = 0;
    cursor_y = 0;
    console_color = VGA_DEFAULT_COLOR;
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga[i] = (uint16_t)(VGA_DEFAULT_COLOR << 8) | ' ';
    }
}

static uint8_t logo_color(char c)
{
    switch (c) {
    case '$': return 0x0e;
    case 'X': return 0x0f;
    case 'x': return 0x0b;
    case '+': return 0x0a;
    case ':': return 0x09;
    case ';': return 0x0d;
    default: return 0x00;
    }
}

static uint8_t logo_weight(char c)
{
    switch (c) {
    case '$': return 6;
    case 'X': return 5;
    case 'x': return 4;
    case '+': return 3;
    case ':': return 2;
    case ';': return 1;
    default: return 0;
    }
}

/* Downsample the source logo to a width x height grid and return the
 * dominant (highest-weight) source character for cell (x, y). */
static char logo_sample(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    uint32_t source_y0 = y * perwix_logo_rows / height;
    uint32_t source_y1 = (y + 1) * perwix_logo_rows / height;
    if (source_y1 <= source_y0) {
        source_y1 = source_y0 + 1;
    }
    uint32_t source_x0 = x * perwix_logo_width / width;
    uint32_t source_x1 = (x + 1) * perwix_logo_width / width;
    if (source_x1 <= source_x0) {
        source_x1 = source_x0 + 1;
    }

    char pixel = '.';
    uint8_t weight = 0;
    for (uint32_t sy = source_y0; sy < source_y1; ++sy) {
        for (uint32_t sx = source_x0; sx < source_x1; ++sx) {
            char candidate = perwix_logo[sy][sx];
            uint8_t candidate_weight = logo_weight(candidate);
            if (candidate_weight > weight) {
                pixel = candidate;
                weight = candidate_weight;
            }
        }
    }
    return pixel;
}

void console_draw_logo(void)
{
    const uint32_t left = (VGA_WIDTH - LOGO_WIDTH) / 2;

    for (uint32_t y = 0; y < LOGO_HEIGHT; ++y) {
        for (uint32_t x = 0; x < LOGO_WIDTH; ++x) {
            char pixel = logo_sample(x, y, LOGO_WIDTH, LOGO_HEIGHT);
            char output = pixel == '.' ? ' ' : pixel;
            vga[y * VGA_WIDTH + left + x] =
                (uint16_t)(logo_color(pixel) << 8) | (uint8_t)output;
        }
    }

    cursor_x = 0;
    cursor_y = LOGO_HEIGHT + 1;
}

/* Write one row of the logo at the cursor (no newline), scaled to
 * width x height cells, using the same colours as the boot logo. */
void console_write_logo_row(uint32_t row, uint32_t width, uint32_t height)
{
    uint8_t saved = console_color;
    for (uint32_t x = 0; x < width; ++x) {
        char pixel = logo_sample(x, row, width, height);
        char text[2] = { pixel == '.' ? ' ' : pixel, '\0' };
        console_color = logo_color(pixel);
        console_write(text);
    }
    console_color = saved;
}

void console_set_color(uint8_t foreground)
{
    console_color = foreground & 0x0f;
}

/* VGA text mode is an 8-bit character-cell interface, not a Unicode
 * framebuffer. Preserve UTF-8 byte-for-byte on COM1 (so a UTF-8 terminal
 * renders it correctly), and show one safe placeholder per non-ASCII code
 * point on VGA rather than corrupting the cursor with each UTF-8 byte. */
void console_write(const char *text)
{
    uint64_t flags = irq_save();
    while (*text != '\0') {
        uint8_t lead = (uint8_t)*text;
        if (lead < 0x80U) {
            char c = *text++;
            if (c == '\n') {
                serial_putc('\r');
            }
            serial_putc(c);
            vga_putc(c);
            continue;
        }

        uint32_t length = 0;
        if (lead >= 0xc2U && lead <= 0xdfU) length = 2;
        else if (lead >= 0xe0U && lead <= 0xefU) length = 3;
        else if (lead >= 0xf0U && lead <= 0xf4U) length = 4;

        if (length == 0) {
            serial_putc(*text++);
            vga_putc('?');
            continue;
        }

        bool valid = true;
        for (uint32_t i = 1; i < length; ++i) {
            uint8_t continuation = (uint8_t)text[i];
            if (continuation == 0 || (continuation & 0xc0U) != 0x80U) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            serial_putc(*text++);
            vga_putc('?');
            continue;
        }
        for (uint32_t i = 0; i < length; ++i) {
            serial_putc(text[i]);
        }
        text += length;
        vga_putc('?');
    }
    irq_restore(flags);
}

void console_write_u64(uint64_t value)
{
    char digits[21];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(digits));

    char output[21];
    for (uint32_t i = 0; i < count; ++i) {
        output[i] = digits[count - i - 1];
    }
    output[count] = '\0';
    console_write(output);
}

void console_write_hex(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    char output[19] = "0x";
    for (uint32_t i = 0; i < 16; ++i) {
        output[2 + i] = hex[(value >> (60 - i * 4)) & 0xf];
    }
    output[18] = '\0';
    console_write(output);
}

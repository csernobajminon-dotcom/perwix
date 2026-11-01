#include "perwix.h"
#include "../drivers/framebuffer.h"
#include "../lib/utf8.h"

#define COM1 0x3f8
#define VGA_WIDTH 80U
#define VGA_HEIGHT 25U
#define MAX_COLUMNS 160U
#define MAX_ROWS 60U
#define VGA_DEFAULT_COLOR 0x0fU
#define LOGO_WIDTH 24U
#define LOGO_HEIGHT 10U
#define SCROLLBACK_LINES 256U
#define CELL_COUNT (MAX_COLUMNS * MAX_ROWS)
#define CELL_CODEPOINT_MASK 0x001fffffU
#define CELL_COLOR_SHIFT 24U

static volatile uint16_t *const vga_hw =
    (volatile uint16_t *)(uintptr_t)0xb8000;
static uint32_t cells[CELL_COUNT];
static uint32_t cursor_x;
static uint32_t cursor_y;
static uint32_t console_columns = VGA_WIDTH;
static uint32_t console_rows = VGA_HEIGHT;
static uint8_t console_color = VGA_DEFAULT_COLOR;
static uint32_t scrollback[SCROLLBACK_LINES][MAX_COLUMNS];
static uint32_t scrollback_head;
static uint32_t scrollback_count;
static uint32_t scrollback_offset;

extern const char perwix_logo[][101];
extern const uint32_t perwix_logo_rows;
extern const uint32_t perwix_logo_width;

static uint32_t make_cell(uint32_t codepoint, uint8_t color)
{
    return ((uint32_t)(color & 0x0fU) << CELL_COLOR_SHIFT) |
           (codepoint & CELL_CODEPOINT_MASK);
}

static uint32_t cell_codepoint(uint32_t cell)
{
    return cell & CELL_CODEPOINT_MASK;
}

static uint8_t cell_color(uint32_t cell)
{
    return (uint8_t)((cell >> CELL_COLOR_SHIFT) & 0x0fU);
}

static void render_cell(uint32_t index)
{
    uint32_t cell;
    uint32_t codepoint;
    uint8_t color;
    uint32_t column;
    uint32_t row;

    if (index >= console_columns * console_rows) {
        return;
    }

    cell = cells[index];
    codepoint = cell_codepoint(cell);
    color = cell_color(cell);
    column = index % console_columns;
    row = index / console_columns;
    if (framebuffer_is_active()) {
        framebuffer_draw_cell(column, row, codepoint, color);
    } else {
        uint8_t character = (codepoint >= 0x20U && codepoint <= 0x7eU) ?
                            (uint8_t)codepoint : (uint8_t)'?';
        uint32_t vga_index = row * VGA_WIDTH + column;
        if (column < VGA_WIDTH && row < VGA_HEIGHT) {
            vga_hw[vga_index] =
                (uint16_t)(((uint16_t)color << 8) | character);
        }
    }
}

static void render_all(void)
{
    for (uint32_t i = 0; i < console_columns * console_rows; ++i) {
        render_cell(i);
    }
}

static void serial_putc(char c)
{
    for (uint32_t tries = 0; tries < 10000U; ++tries) {
        if ((inb(COM1 + 5) & 0x20U) != 0U) {
            outb(COM1, (uint8_t)c);
            return;
        }
    }
}

static void scroll(void)
{
    for (uint32_t x = 0; x < console_columns; ++x) {
        scrollback[scrollback_head][x] = cells[x];
    }
    scrollback_head = (scrollback_head + 1U) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        ++scrollback_count;
    }
    scrollback_offset = 0;

    for (uint32_t y = 1; y < console_rows; ++y) {
        for (uint32_t x = 0; x < console_columns; ++x) {
            cells[(y - 1U) * console_columns + x] =
                cells[y * console_columns + x];
        }
    }
    for (uint32_t x = 0; x < console_columns; ++x) {
        cells[(console_rows - 1U) * console_columns + x] =
            make_cell(' ', VGA_DEFAULT_COLOR);
    }
    cursor_y = console_rows - 1U;
    render_all();
}

static void put_codepoint(uint32_t codepoint)
{
    if (codepoint == '\r') {
        cursor_x = 0;
        return;
    }

    scrollback_offset = 0;
    if (codepoint == '\n') {
        cursor_x = 0;
        ++cursor_y;
    } else if (codepoint == '\b') {
        if (cursor_x != 0U || cursor_y != 0U) {
            if (cursor_x == 0U) {
                --cursor_y;
                cursor_x = console_columns - 1U;
            } else {
                --cursor_x;
            }
            uint32_t index = cursor_y * console_columns + cursor_x;
            cells[index] = make_cell(' ', console_color);
            render_cell(index);
        }
    } else if (codepoint == '\t') {
        uint32_t spaces = 4U - (cursor_x & 3U);
        for (uint32_t i = 0U; i < spaces; ++i) {
            put_codepoint(' ');
        }
    } else {
        uint32_t index = cursor_y * console_columns + cursor_x;
        cells[index] = make_cell(codepoint, console_color);
        render_cell(index);
        ++cursor_x;
        if (cursor_x == console_columns) {
            cursor_x = 0;
            ++cursor_y;
        }
    }

    if (cursor_y >= console_rows) {
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
    for (uint32_t i = 0; i < CELL_COUNT; ++i) {
        cells[i] = make_cell(' ', VGA_DEFAULT_COLOR);
    }
    render_all();
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

    int32_t end = (int32_t)(scrollback_count + console_rows - 1U) - next;
    int32_t start = end - ((int32_t)console_rows - 1);
    uint32_t oldest = (scrollback_head + SCROLLBACK_LINES -
                       scrollback_count) % SCROLLBACK_LINES;
    for (uint32_t y = 0; y < console_rows; ++y) {
        int32_t row = start + (int32_t)y;
        for (uint32_t x = 0; x < console_columns; ++x) {
            uint32_t cell = make_cell(' ', VGA_DEFAULT_COLOR);
            if (row >= 0 && (uint32_t)row < scrollback_count) {
                uint32_t index = (oldest + (uint32_t)row) % SCROLLBACK_LINES;
                cell = scrollback[index][x];
            } else if (row >= (int32_t)scrollback_count &&
                       row < (int32_t)(scrollback_count + console_rows)) {
                uint32_t live_row = (uint32_t)(row - (int32_t)scrollback_count);
                cell = cells[live_row * console_columns + x];
            }
            cells[y * console_columns + x] = cell;
        }
    }
    render_all();
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
    scrollback_head = 0;
    scrollback_count = 0;
    scrollback_offset = 0;
    if (framebuffer_is_active()) {
        console_columns = framebuffer_width() / 8U;
        console_rows = framebuffer_height() / 16U;
        if (console_columns > MAX_COLUMNS) console_columns = MAX_COLUMNS;
        if (console_rows > MAX_ROWS) console_rows = MAX_ROWS;
        if (console_columns < VGA_WIDTH) console_columns = VGA_WIDTH;
        if (console_rows < VGA_HEIGHT) console_rows = VGA_HEIGHT;
        framebuffer_set_console_geometry(console_columns, console_rows);
    } else {
        console_columns = VGA_WIDTH;
        console_rows = VGA_HEIGHT;
    }
    for (uint32_t i = 0; i < CELL_COUNT; ++i) {
        cells[i] = make_cell(' ', VGA_DEFAULT_COLOR);
    }
    render_all();
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
    uint32_t source_y1 = (y + 1U) * perwix_logo_rows / height;
    if (source_y1 <= source_y0) {
        source_y1 = source_y0 + 1U;
    }
    uint32_t source_x0 = x * perwix_logo_width / width;
    uint32_t source_x1 = (x + 1U) * perwix_logo_width / width;
    if (source_x1 <= source_x0) {
        source_x1 = source_x0 + 1U;
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
    const uint32_t left = (console_columns - LOGO_WIDTH) / 2U;

    for (uint32_t y = 0; y < LOGO_HEIGHT; ++y) {
        for (uint32_t x = 0; x < LOGO_WIDTH; ++x) {
            char pixel = logo_sample(x, y, LOGO_WIDTH, LOGO_HEIGHT);
            uint32_t index = y * console_columns + left + x;
            cells[index] = make_cell(pixel == '.' ? ' ' : (uint8_t)pixel,
                                     logo_color(pixel));
            render_cell(index);
        }
    }

    cursor_x = 0;
    cursor_y = LOGO_HEIGHT + 1U;
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
    console_color = foreground & 0x0fU;
}

/* Render valid UTF-8 as Unicode code points. Invalid byte sequences become
 * U+FFFD on both the framebuffer and serial console. */
void console_write(const char *text)
{
    if (text == 0) return;
    uint64_t flags = irq_save();
    while (*text != '\0') {
        uint32_t codepoint = 0xfffdU;
        uint32_t consumed = 1U;
        bool valid = utf8_decode_one((const uint8_t *)text,
                                     &codepoint, &consumed);
        if (consumed == 0U) {
            break;
        }
        if (!valid) {
            char replacement[4];
            uint32_t replacement_length = utf8_encode_one(0xfffdU, replacement);
            for (uint32_t i = 0U; i < replacement_length; ++i) {
                serial_putc(replacement[i]);
            }
            put_codepoint(0xfffdU);
            ++text;
            continue;
        }

        for (uint32_t i = 0; i < consumed; ++i) {
            if (text[i] == '\n') {
                serial_putc('\r');
            }
            serial_putc(text[i]);
        }
        text += consumed;
        put_codepoint(codepoint);
    }
    irq_restore(flags);
}

void console_write_u64(uint64_t value)
{
    char digits[21];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));

    char output[21];
    for (uint32_t i = 0; i < count; ++i) {
        output[i] = digits[count - i - 1U];
    }
    output[count] = '\0';
    console_write(output);
}

void console_write_hex(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    char output[19] = "0x";
    for (uint32_t i = 0; i < 16U; ++i) {
        output[2U + i] = hex[(value >> (60U - i * 4U)) & 0xfU];
    }
    output[18] = '\0';
    console_write(output);
}

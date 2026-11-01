#include "framebuffer.h"
#include "../core/font8x16.h"

#define MULTIBOOT2_TAG_END 0U
#define MULTIBOOT2_TAG_FRAMEBUFFER 8U
#define MULTIBOOT2_MAX_INFO_SIZE (16U * 1024U * 1024U)
#define PERWIX_TEXT_COLUMNS 80U
#define PERWIX_TEXT_ROWS 25U
#define FONT_WIDTH 8U
#define FONT_HEIGHT 16U
#define MAX_TEXT_COLUMNS 160U
#define MAX_TEXT_ROWS 60U

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
};

struct __attribute__((packed)) multiboot2_framebuffer_tag {
    uint32_t type;
    uint32_t size;
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bits_per_pixel;
    uint8_t framebuffer_type;
    uint16_t reserved;
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
};

struct framebuffer_state {
    volatile uint8_t *base;
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t bytes_per_pixel;
    uint8_t red_position;
    uint8_t red_size;
    uint8_t green_position;
    uint8_t green_size;
    uint8_t blue_position;
    uint8_t blue_size;
    bool active;
};

static struct framebuffer_state fb;
static uint32_t text_columns = 80U;
static uint32_t text_rows = 25U;

static const uint32_t vga_palette[16] = {
    0x000000U, 0x0000aaU, 0x00aa00U, 0x00aaaaU,
    0xaa0000U, 0xaa00aaU, 0xaa5500U, 0xaaaaaaU,
    0x555555U, 0x5555ffU, 0x55ff55U, 0x55ffffU,
    0xff5555U, 0xff55ffU, 0xffff55U, 0xffffffU
};

static bool field_fits(uint8_t position, uint8_t size, uint8_t bpp)
{
    return size != 0U && size <= 8U && position < bpp &&
           (uint16_t)position + (uint16_t)size <= (uint16_t)bpp;
}

static uint32_t scale_channel(uint8_t value, uint8_t bits)
{
    uint32_t maximum = (1U << bits) - 1U;
    return ((uint32_t)value * maximum + 127U) / 255U;
}

static uint32_t pack_rgb(uint32_t rgb)
{
    uint8_t red = (uint8_t)(rgb >> 16);
    uint8_t green = (uint8_t)(rgb >> 8);
    uint8_t blue = (uint8_t)rgb;
    return (scale_channel(red, fb.red_size) << fb.red_position) |
           (scale_channel(green, fb.green_size) << fb.green_position) |
           (scale_channel(blue, fb.blue_size) << fb.blue_position);
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    uint32_t packed;
    volatile uint8_t *pixel;

    if (!fb.active || x >= fb.width || y >= fb.height) {
        return;
    }

    packed = pack_rgb(rgb);
    pixel = fb.base + (uint64_t)y * fb.pitch + (uint64_t)x * fb.bytes_per_pixel;

    if (fb.bytes_per_pixel == 4U) {
        *(volatile uint32_t *)(void *)pixel = packed;
    } else if (fb.bytes_per_pixel == 3U) {
        pixel[0] = (uint8_t)packed;
        pixel[1] = (uint8_t)(packed >> 8);
        pixel[2] = (uint8_t)(packed >> 16);
    } else {
        *(volatile uint16_t *)(void *)pixel = (uint16_t)packed;
    }
}

bool framebuffer_init_multiboot2(uint64_t multiboot_info_address)
{
    const uint8_t *info;
    uint32_t total_size;
    uint32_t offset;

    fb.active = false;
    fb.base = 0;
    if (multiboot_info_address == 0U ||
        multiboot_info_address >= 0x100000000ULL) {
        return false;
    }

    info = (const uint8_t *)(uintptr_t)multiboot_info_address;
    total_size = *(const uint32_t *)(const void *)info;
    if (total_size < 16U || total_size > MULTIBOOT2_MAX_INFO_SIZE ||
        multiboot_info_address + total_size > 0x100000000ULL) {
        return false;
    }

    offset = 8U;
    while (offset <= total_size - 8U) {
        const struct multiboot2_tag *tag =
            (const struct multiboot2_tag *)(const void *)(info + offset);
        uint32_t aligned_size;

        if (tag->size < 8U || tag->size > total_size - offset) {
            return false;
        }
        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }

        if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER &&
            tag->size >= sizeof(struct multiboot2_framebuffer_tag)) {
            const struct multiboot2_framebuffer_tag *mode =
                (const struct multiboot2_framebuffer_tag *)(const void *)tag;
            uint64_t framebuffer_size;
            uint64_t framebuffer_end;
            uint32_t bytes_per_pixel;

            if (mode->framebuffer_type != 1U ||
                (mode->bits_per_pixel != 16U &&
                 mode->bits_per_pixel != 24U &&
                 mode->bits_per_pixel != 32U) ||
                mode->width < PERWIX_TEXT_COLUMNS * FONT_WIDTH ||
                mode->height < PERWIX_TEXT_ROWS * FONT_HEIGHT ||
                mode->width > 16384U || mode->height > 16384U) {
                return false;
            }

            bytes_per_pixel = (uint32_t)(mode->bits_per_pixel + 7U) / 8U;
            if (mode->pitch < mode->width * bytes_per_pixel ||
                !field_fits(mode->red_position, mode->red_mask_size,
                            mode->bits_per_pixel) ||
                !field_fits(mode->green_position, mode->green_mask_size,
                            mode->bits_per_pixel) ||
                !field_fits(mode->blue_position, mode->blue_mask_size,
                            mode->bits_per_pixel)) {
                return false;
            }

            framebuffer_size = (uint64_t)mode->pitch * mode->height;
            framebuffer_end = mode->address + framebuffer_size;
            /* The bootstrap page tables identity-map physical memory below 4 GiB. */
            if (mode->address == 0U || framebuffer_end < mode->address ||
                framebuffer_end > 0x100000000ULL) {
                return false;
            }

            fb.base = (volatile uint8_t *)(uintptr_t)mode->address;
            fb.address = mode->address;
            fb.pitch = mode->pitch;
            fb.width = mode->width;
            fb.height = mode->height;
            fb.bpp = mode->bits_per_pixel;
            fb.bytes_per_pixel = (uint8_t)bytes_per_pixel;
            fb.red_position = mode->red_position;
            fb.red_size = mode->red_mask_size;
            fb.green_position = mode->green_position;
            fb.green_size = mode->green_mask_size;
            fb.blue_position = mode->blue_position;
            fb.blue_size = mode->blue_mask_size;
            fb.active = true;
            framebuffer_clear(0U);
            return true;
        }

        aligned_size = (tag->size + 7U) & ~7U;
        if (aligned_size > total_size - offset) {
            break;
        }
        offset += aligned_size;
    }

    return false;
}

bool framebuffer_is_active(void)
{
    return fb.active;
}

uint32_t framebuffer_width(void)
{
    return fb.active ? fb.width : 0U;
}

uint32_t framebuffer_height(void)
{
    return fb.active ? fb.height : 0U;
}

uint8_t framebuffer_bits_per_pixel(void)
{
    return fb.active ? fb.bpp : 0U;
}

void framebuffer_set_console_geometry(uint32_t columns, uint32_t rows)
{
    if (columns == 0U || rows == 0U ||
        columns > MAX_TEXT_COLUMNS || rows > MAX_TEXT_ROWS) {
        return;
    }
    text_columns = columns;
    text_rows = rows;
}

void framebuffer_clear(uint8_t color_index)
{
    uint32_t color;
    if (!fb.active) {
        return;
    }
    color = vga_palette[color_index & 0x0fU];
    for (uint32_t y = 0; y < fb.height; ++y) {
        for (uint32_t x = 0; x < fb.width; ++x) {
            put_pixel(x, y, color);
        }
    }
}

static uint32_t glyph_index(uint32_t codepoint)
{
    if (codepoint >= 0x20U && codepoint <= 0x7eU) {
        return codepoint - 0x20U;
    }
    if (codepoint >= 0x400U && codepoint <= 0x45fU) {
        return 95U + codepoint - 0x400U;
    }
    if (codepoint == 0x490U) {
        return 191U;
    }
    if (codepoint == 0x491U) {
        return 192U;
    }
    return (uint32_t)'?' - 0x20U;
}

void framebuffer_draw_cell(uint32_t column, uint32_t row,
                           uint32_t codepoint, uint8_t foreground)
{
    uint32_t left;
    uint32_t top;
    uint32_t origin_x;
    uint32_t origin_y;
    uint32_t fg;
    const uint8_t *glyph;

    if (!fb.active || column >= text_columns || row >= text_rows) {
        return;
    }

    /* Draw one bitmap-font pixel per physical framebuffer pixel.  No
     * fractional scaling, filtering, or interpolated colour is used. */
    origin_x = (fb.width - text_columns * FONT_WIDTH) / 2U;
    origin_y = (fb.height - text_rows * FONT_HEIGHT) / 2U;
    left = origin_x + column * FONT_WIDTH;
    top = origin_y + row * FONT_HEIGHT;
    fg = vga_palette[foreground & 0x0fU];
    glyph = perwix_font8x16[glyph_index(codepoint)];

    for (uint32_t py = 0; py < FONT_HEIGHT; ++py) {
        uint8_t bits = glyph[py];
        for (uint32_t px = 0; px < FONT_WIDTH; ++px) {
            bool set = (bits & (uint8_t)(0x80U >> px)) != 0U;
            put_pixel(left + px, top + py, set ? fg : 0x000000U);
        }
    }
}

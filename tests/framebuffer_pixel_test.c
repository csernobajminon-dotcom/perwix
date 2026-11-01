#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/core/font8x16.h"

#define TEST_WIDTH 1280U
#define TEST_HEIGHT 720U
#define TEST_PITCH (TEST_WIDTH * 4U)
#define TEST_FB_BYTES ((size_t)TEST_PITCH * TEST_HEIGHT)

struct __attribute__((packed)) test_framebuffer_tag {
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

struct test_end_tag {
    uint32_t type;
    uint32_t size;
};

static void *map_low_memory(size_t size)
{
    void *memory = mmap(0, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (memory == MAP_FAILED || (uintptr_t)memory >= 0x100000000ULL) {
        if (memory != MAP_FAILED) munmap(memory, size);
        return 0;
    }
    return memory;
}

static void assert_glyph_pixels(uint32_t *pixels, uint32_t column,
                                uint32_t row, uint32_t glyph_index)
{
    const uint32_t left = column * 8U;
    const uint32_t top = row * 16U;
    const uint8_t *glyph = perwix_font8x16[glyph_index];
    for (uint32_t y = 0U; y < 16U; ++y) {
        for (uint32_t x = 0U; x < 8U; ++x) {
            const uint32_t expected =
                (glyph[y] & (uint8_t)(0x80U >> x)) != 0U ? 0x00ffffffU : 0U;
            assert(pixels[(top + y) * TEST_WIDTH + left + x] == expected);
        }
    }
}

int main(void)
{
    uint32_t *pixels = map_low_memory(TEST_FB_BYTES);
    uint8_t *info = map_low_memory(64U);
    assert(pixels != 0 && info != 0);

    *(uint32_t *)(void *)info = 56U;
    *(uint32_t *)(void *)(info + 4U) = 0U;
    struct test_framebuffer_tag *tag =
        (struct test_framebuffer_tag *)(void *)(info + 8U);
    tag->type = 8U;
    tag->size = sizeof(*tag);
    tag->address = (uint64_t)(uintptr_t)pixels;
    tag->pitch = TEST_PITCH;
    tag->width = TEST_WIDTH;
    tag->height = TEST_HEIGHT;
    tag->bits_per_pixel = 32U;
    tag->framebuffer_type = 1U;
    tag->red_position = 16U;
    tag->red_mask_size = 8U;
    tag->green_position = 8U;
    tag->green_mask_size = 8U;
    tag->blue_position = 0U;
    tag->blue_mask_size = 8U;

    struct test_end_tag *end =
        (struct test_end_tag *)(void *)(info + 48U);
    end->type = 0U;
    end->size = sizeof(*end);

    assert(framebuffer_init_multiboot2((uint64_t)(uintptr_t)info));
    assert(framebuffer_is_active());
    assert(framebuffer_width() == TEST_WIDTH);
    assert(framebuffer_height() == TEST_HEIGHT);
    framebuffer_set_console_geometry(160U, 45U);

    framebuffer_draw_cell(1U, 2U, 'A', 15U);
    framebuffer_draw_cell(159U, 44U, 0x0456U, 15U);
    assert_glyph_pixels(pixels, 1U, 2U, (uint32_t)'A' - 0x20U);
    assert_glyph_pixels(pixels, 159U, 44U, 95U + 0x56U);
    assert(pixels[0] == 0U);

    puts("Framebuffer 1:1 pixel test OK (1280x720, 160x45 cells)");
    munmap(info, 64U);
    munmap(pixels, TEST_FB_BYTES);
    return 0;
}

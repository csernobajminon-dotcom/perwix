#ifndef PERWIX_FRAMEBUFFER_H
#define PERWIX_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

/* Uses the display mode already programmed by UEFI/BIOS and GRUB. */
bool framebuffer_init_multiboot2(uint64_t multiboot_info_address);
bool framebuffer_is_active(void);
uint32_t framebuffer_width(void);
uint32_t framebuffer_height(void);
uint8_t framebuffer_bits_per_pixel(void);
void framebuffer_clear(uint8_t color_index);
void framebuffer_set_console_geometry(uint32_t columns, uint32_t rows);
void framebuffer_draw_cell(uint32_t column, uint32_t row,
                           uint32_t codepoint, uint8_t foreground);

#endif

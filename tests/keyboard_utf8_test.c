#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/include/perwix.h"

static uint8_t queue[64];
static uint32_t queue_count;
static uint32_t queue_pos;

uint8_t inb(uint16_t port)
{
    if (port == 0x64U) return queue_pos < queue_count ? 1U : 0U;
    if (port == 0x60U && queue_pos < queue_count) return queue[queue_pos++];
    return 0U;
}

void outb(uint16_t port, uint8_t value) { (void)port; (void)value; }

static void push(const uint8_t *bytes, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) queue[queue_count++] = bytes[i];
}

int main(void)
{
    uint32_t cp = 0U;
    const uint8_t toggle_ua[] = { 0x38U, 0x2aU, 0xaaU, 0xb8U, 0x1fU };
    const uint8_t shifted_i[] = { 0x2aU, 0x1fU, 0xaaU };
    const uint8_t ge_case[] = { 0x2bU, 0x2aU, 0x2bU, 0xaaU };
    const uint8_t toggle_us[] = { 0x38U, 0x2aU, 0xaaU, 0xb8U, 0x1fU };
    const uint8_t toggle_ua_reverse[] = { 0x2aU, 0x38U, 0xaaU, 0xb8U, 0x1fU };
    const uint8_t toggle_ua_repeat[] = { 0x38U, 0x2aU, 0x2aU, 0xaaU, 0xb8U, 0x1fU };
    const uint8_t toggle_ua_right_alt[] = {
        0xe0U, 0x38U, 0x2aU, 0xaaU, 0xe0U, 0xb8U, 0x1fU
    };

    keyboard_init();
    push(toggle_ua, sizeof(toggle_ua));
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 0x0456U);
    assert(keyboard_layout_name()[0] == 'U');

    push(shifted_i, sizeof(shifted_i));
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 0x0406U);

    push(ge_case, sizeof(ge_case));
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 0x0491U);
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 0x0490U);

    push(toggle_us, sizeof(toggle_us));
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 's');
    assert(keyboard_layout_name()[0] == 'U' && keyboard_layout_name()[1] == 'S');

    keyboard_init(); /* discard the earlier test's pending key-release byte */
    push(toggle_ua_reverse, sizeof(toggle_ua_reverse));
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 0x0456U);
    assert(keyboard_layout_name()[0] == 'U');

    keyboard_init();
    push(toggle_ua_repeat, sizeof(toggle_ua_repeat));
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 0x0456U);

    keyboard_init();
    push(toggle_ua_right_alt, sizeof(toggle_ua_right_alt));
    assert(keyboard_read_codepoint(&cp) == PERWIX_KEY_CHAR);
    assert(cp == 0x0456U);
    puts("Keyboard US/UA UTF-8 tests OK");
    return 0;
}

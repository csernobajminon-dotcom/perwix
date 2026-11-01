#include "perwix.h"

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

static bool mouse_present;
static bool wheel_present;
static uint8_t packet_size = 3;
static uint8_t packet_index;
static uint8_t packet[4];

static bool wait_input_empty(void)
{
    for (uint32_t tries = 0; tries < 100000; ++tries) {
        if ((inb(KBD_STATUS_PORT) & 0x02) == 0) {
            return true;
        }
    }
    return false;
}

static bool wait_aux_byte(uint8_t *value)
{
    for (uint32_t tries = 0; tries < 100000; ++tries) {
        uint8_t status = inb(KBD_STATUS_PORT);
        if ((status & 0x21) == 0x21) {
            *value = inb(KBD_DATA_PORT);
            return true;
        }
    }
    return false;
}

static bool mouse_write(uint8_t value)
{
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        uint8_t reply;
        if (!wait_input_empty()) {
            return false;
        }
        outb(KBD_STATUS_PORT, 0xd4);
        if (!wait_input_empty()) {
            return false;
        }
        outb(KBD_DATA_PORT, value);
        if (!wait_aux_byte(&reply)) {
            return false;
        }
        if (reply == 0xfa) {
            return true;
        }
        if (reply != 0xfe) {
            return false;
        }
    }
    return false;
}

static bool set_sample_rate(uint8_t rate)
{
    return mouse_write(0xf3) && mouse_write(rate);
}

bool mouse_init(void)
{
    mouse_present = false;
    wheel_present = false;
    packet_index = 0;

    if (!wait_input_empty()) {
        return false;
    }
    outb(KBD_STATUS_PORT, 0xa8);
    if (!wait_input_empty()) {
        return false;
    }
    outb(KBD_STATUS_PORT, 0x20);

    uint8_t command_byte;
    for (uint32_t tries = 0; tries < 100000; ++tries) {
        if ((inb(KBD_STATUS_PORT) & 1) != 0) {
            command_byte = inb(KBD_DATA_PORT);
            goto have_command_byte;
        }
    }
    return false;

have_command_byte:
    command_byte = (uint8_t)((command_byte | 0x02) & ~0x20);
    if (!wait_input_empty()) {
        return false;
    }
    outb(KBD_STATUS_PORT, 0x60);
    if (!wait_input_empty()) {
        return false;
    }
    outb(KBD_DATA_PORT, command_byte);

    if (!mouse_write(0xf6) || !set_sample_rate(200) ||
        !set_sample_rate(100) || !set_sample_rate(80) ||
        !mouse_write(0xf2)) {
        return false;
    }

    uint8_t device_id;
    if (!wait_aux_byte(&device_id)) {
        return false;
    }
    mouse_present = true;
    wheel_present = device_id == 3 || device_id == 4;
    packet_size = wheel_present ? 4 : 3;
    return true;
}

bool mouse_has_wheel(void)
{
    return mouse_present && wheel_present;
}

void mouse_poll(void)
{
    if (!mouse_present) {
        return;
    }
    while ((inb(KBD_STATUS_PORT) & 0x21) == 0x21) {
        uint8_t value = inb(KBD_DATA_PORT);
        if (packet_index == 0 && (value & 0x08) == 0) {
            continue;
        }
        packet[packet_index++] = value;
        if (packet_index < packet_size) {
            continue;
        }
        packet_index = 0;
        if (wheel_present) {
            int32_t wheel = packet[3] & 0x0f;
            if ((wheel & 0x08) != 0) {
                wheel -= 16;
            }
            if (wheel != 0) {
                console_scroll_view(wheel > 0 ? 3 : -3);
            }
        }
    }
}

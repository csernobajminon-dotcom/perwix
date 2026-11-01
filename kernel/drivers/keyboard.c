#include "perwix.h"

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

static bool shift_down;
static bool caps_lock;
static bool alt_down;
static bool extended_key;
static bool ukrainian_layout;
static bool layout_chord_latched;

static void update_layout_chord(void)
{
    if (alt_down && shift_down && !layout_chord_latched) {
        ukrainian_layout = !ukrainian_layout;
        layout_chord_latched = true;
    }
}

static const uint32_t us_keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x0f] = '\t', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u',
    [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1a] = '[',
    [0x1b] = ']', [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x',
    [0x2e] = 'c', [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

static const uint32_t us_shifted_keymap[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}',
    [0x1e] = 'A', [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2b] = '|', [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C',
    [0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
};

/* Ukrainian standard keyboard positions (ЙЦУКЕН layout). */
static const uint32_t ua_keymap[128] = {
    [0x10] = 0x0439U, [0x11] = 0x0446U, [0x12] = 0x0443U,
    [0x13] = 0x043AU, [0x14] = 0x0435U, [0x15] = 0x043DU,
    [0x16] = 0x0433U, [0x17] = 0x0448U, [0x18] = 0x0449U,
    [0x19] = 0x0437U, [0x1a] = 0x0445U, [0x1b] = 0x0457U,
    [0x1e] = 0x0444U, [0x1f] = 0x0456U, [0x20] = 0x0432U,
    [0x21] = 0x0430U, [0x22] = 0x043FU, [0x23] = 0x0440U,
    [0x24] = 0x043EU, [0x25] = 0x043BU, [0x26] = 0x0434U,
    [0x27] = 0x0436U, [0x28] = 0x0454U, [0x2b] = 0x0491U,
    [0x2c] = 0x044FU, [0x2d] = 0x0447U, [0x2e] = 0x0441U,
    [0x2f] = 0x043CU, [0x30] = 0x0438U, [0x31] = 0x0442U,
    [0x32] = 0x044CU, [0x33] = 0x0431U, [0x34] = 0x044EU,
};

static uint32_t ukrainian_upper(uint32_t cp)
{
    switch (cp) {
    case 0x0454U: return 0x0404U; /* Є */
    case 0x0456U: return 0x0406U; /* І */
    case 0x0457U: return 0x0407U; /* Ї */
    case 0x0491U: return 0x0490U; /* Ґ */
    default:
        if (cp >= 0x0430U && cp <= 0x044fU) return cp - 0x20U;
        return cp;
    }
}

void keyboard_init(void)
{
    shift_down = false;
    caps_lock = false;
    alt_down = false;
    extended_key = false;
    ukrainian_layout = false;
    layout_chord_latched = false;
    for (uint32_t i = 0; i < 32U && (inb(KBD_STATUS_PORT) & 1U) != 0U; ++i) {
        (void)inb(KBD_DATA_PORT);
    }
}

uint8_t keyboard_read_codepoint(uint32_t *codepoint)
{
    if (codepoint == 0) return PERWIX_KEY_NONE;

    while ((inb(KBD_STATUS_PORT) & 1U) != 0U) {
        if ((inb(KBD_STATUS_PORT) & 0x20U) != 0U) {
            return PERWIX_KEY_NONE;
        }
        uint8_t scan = inb(KBD_DATA_PORT);
        if (scan == 0xe0U) {
            extended_key = true;
            continue;
        }

        bool extended = extended_key;
        extended_key = false;

        if (!extended && (scan == 0x2aU || scan == 0x36U)) {
            shift_down = true;
            update_layout_chord();
            continue;
        }
        if (!extended && (scan == 0xaaU || scan == 0xb6U)) {
            shift_down = false;
            if (!alt_down) layout_chord_latched = false;
            continue;
        }
        if ((!extended && scan == 0x38U) ||
            (extended && scan == 0x38U)) {
            alt_down = true;
            update_layout_chord();
            continue;
        }
        if ((!extended && scan == 0xb8U) ||
            (extended && scan == 0xb8U)) {
            alt_down = false;
            if (!shift_down) layout_chord_latched = false;
            continue;
        }
        if ((scan & 0x80U) != 0U) {
            continue;
        }

        if (extended) {
            switch (scan) {
            case 0x48: return PERWIX_KEY_UP;
            case 0x50: return PERWIX_KEY_DOWN;
            case 0x4b: return PERWIX_KEY_LEFT;
            case 0x4d: return PERWIX_KEY_RIGHT;
            case 0x1c:
                *codepoint = '\n';
                return PERWIX_KEY_CHAR;
            default:
                continue;
            }
        }

        if (scan == 0x01U) return PERWIX_KEY_ESCAPE;
        if (scan == 0x3aU) {
            caps_lock = !caps_lock;
            continue;
        }
        if (scan == 0x1cU) {
            *codepoint = '\n';
            return PERWIX_KEY_CHAR;
        }
        if (scan == 0x0eU) {
            *codepoint = '\b';
            return PERWIX_KEY_CHAR;
        }

        if (ukrainian_layout && ua_keymap[scan] != 0U) {
            uint32_t cp = ua_keymap[scan];
            if (shift_down != caps_lock) cp = ukrainian_upper(cp);
            *codepoint = cp;
            return PERWIX_KEY_CHAR;
        }

        uint32_t value = us_keymap[scan];
        if (value >= 'a' && value <= 'z') {
            if (shift_down != caps_lock) value -= 'a' - 'A';
        } else if (shift_down && us_shifted_keymap[scan] != 0U) {
            value = us_shifted_keymap[scan];
        }
        if (value != 0U) {
            *codepoint = value;
            return PERWIX_KEY_CHAR;
        }
    }
    return PERWIX_KEY_NONE;
}

uint8_t keyboard_read_key(char *character)
{
    uint32_t cp = 0U;
    uint8_t key = keyboard_read_codepoint(&cp);
    if (key == PERWIX_KEY_CHAR && character != 0) {
        *character = cp <= 0x7fU ? (char)cp : '?';
    }
    return key;
}

bool keyboard_read_char(char *character)
{
    return keyboard_read_key(character) == PERWIX_KEY_CHAR;
}

const char *keyboard_layout_name(void)
{
    return ukrainian_layout ? "Ukrainian" : "US";
}

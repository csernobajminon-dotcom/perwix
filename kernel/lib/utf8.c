#include "utf8.h"

bool utf8_decode_one(const uint8_t *input, uint32_t *codepoint,
                     uint32_t *consumed)
{
    uint8_t a;
    uint32_t cp;
    uint32_t length;

    if (input == 0 || codepoint == 0 || consumed == 0 || input[0] == 0U) {
        if (consumed != 0) *consumed = 0U;
        return false;
    }

    a = input[0];
    if (a < 0x80U) {
        *codepoint = a;
        *consumed = 1U;
        return true;
    }
    if (a >= 0xc2U && a <= 0xdfU) {
        cp = (uint32_t)(a & 0x1fU);
        length = 2U;
    } else if (a >= 0xe0U && a <= 0xefU) {
        cp = (uint32_t)(a & 0x0fU);
        length = 3U;
    } else if (a >= 0xf0U && a <= 0xf4U) {
        cp = (uint32_t)(a & 0x07U);
        length = 4U;
    } else {
        *codepoint = 0xfffdU;
        *consumed = 1U;
        return false;
    }

    for (uint32_t i = 1U; i < length; ++i) {
        uint8_t c = input[i];
        if (c == 0U || (c & 0xc0U) != 0x80U) {
            *codepoint = 0xfffdU;
            *consumed = 1U;
            return false;
        }
        if (i == 1U) {
            if ((a == 0xe0U && c < 0xa0U) ||
                (a == 0xedU && c >= 0xa0U) ||
                (a == 0xf0U && c < 0x90U) ||
                (a == 0xf4U && c >= 0x90U)) {
                *codepoint = 0xfffdU;
                *consumed = 1U;
                return false;
            }
        }
        cp = (cp << 6) | (uint32_t)(c & 0x3fU);
    }

    if (cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) {
        *codepoint = 0xfffdU;
        *consumed = 1U;
        return false;
    }

    *codepoint = cp;
    *consumed = length;
    return true;
}

uint32_t utf8_encode_one(uint32_t cp, char output[4])
{
    if (output == 0 || cp > 0x10ffffU ||
        (cp >= 0xd800U && cp <= 0xdfffU)) {
        return 0U;
    }
    if (cp < 0x80U) {
        output[0] = (char)cp;
        return 1U;
    }
    if (cp < 0x800U) {
        output[0] = (char)(0xc0U | (cp >> 6));
        output[1] = (char)(0x80U | (cp & 0x3fU));
        return 2U;
    }
    if (cp < 0x10000U) {
        output[0] = (char)(0xe0U | (cp >> 12));
        output[1] = (char)(0x80U | ((cp >> 6) & 0x3fU));
        output[2] = (char)(0x80U | (cp & 0x3fU));
        return 3U;
    }
    output[0] = (char)(0xf0U | (cp >> 18));
    output[1] = (char)(0x80U | ((cp >> 12) & 0x3fU));
    output[2] = (char)(0x80U | ((cp >> 6) & 0x3fU));
    output[3] = (char)(0x80U | (cp & 0x3fU));
    return 4U;
}

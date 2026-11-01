#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/lib/utf8.h"

int main(void)
{
    const uint8_t ukrainian[] = "Україна";
    const uint8_t invalid_overlong[] = { 0xc0U, 0xafU, 0U };
    const uint8_t invalid_surrogate[] = { 0xedU, 0xa0U, 0x80U, 0U };
    const uint8_t invalid_continuation[] = { 0xe2U, 0x28U, 0xa1U, 0U };
    const uint8_t truncated[] = { 0xf0U, 0x9fU, 0x92U, 0U };
    const uint8_t valid_emoji[] = { 0xf0U, 0x9fU, 0x98U, 0x80U, 0U };
    const uint8_t maximum_codepoint[] = { 0xf4U, 0x8fU, 0xbfU, 0xbfU, 0U };
    uint32_t cp = 0U;
    uint32_t consumed = 0U;
    char encoded[4];

    assert(utf8_decode_one(ukrainian, &cp, &consumed));
    assert(cp == 0x0423U && consumed == 2U);
    assert(utf8_decode_one(ukrainian + 2U, &cp, &consumed));
    assert(cp == 0x043aU && consumed == 2U);
    assert(!utf8_decode_one(invalid_overlong, &cp, &consumed));
    assert(cp == 0xfffdU && consumed == 1U);
    assert(!utf8_decode_one(invalid_surrogate, &cp, &consumed));
    assert(cp == 0xfffdU && consumed == 1U);
    assert(!utf8_decode_one(invalid_continuation, &cp, &consumed));
    assert(cp == 0xfffdU && consumed == 1U);
    assert(!utf8_decode_one(truncated, &cp, &consumed));
    assert(cp == 0xfffdU && consumed == 1U);
    assert(utf8_decode_one(valid_emoji, &cp, &consumed));
    assert(cp == 0x1f600U && consumed == 4U);
    assert(utf8_decode_one(maximum_codepoint, &cp, &consumed));
    assert(cp == 0x10ffffU && consumed == 4U);
    assert(utf8_encode_one(0x1f600U, encoded) == 4U);
    assert((uint8_t)encoded[0] == 0xf0U && (uint8_t)encoded[1] == 0x9fU &&
           (uint8_t)encoded[2] == 0x98U && (uint8_t)encoded[3] == 0x80U);
    assert(utf8_encode_one(0xd800U, encoded) == 0U);
    assert(utf8_encode_one(0x110000U, encoded) == 0U);
    assert(utf8_encode_one(0x0491U, encoded) == 2U);
    assert((uint8_t)encoded[0] == 0xd2U && (uint8_t)encoded[1] == 0x91U);
    puts("UTF-8 tests OK");
    return 0;
}

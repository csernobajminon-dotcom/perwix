#ifndef PERWIX_UTF8_H
#define PERWIX_UTF8_H

#include <stdbool.h>
#include <stdint.h>

/* Strict UTF-8 decoder. Invalid input returns U+FFFD and consumes one byte. */
bool utf8_decode_one(const uint8_t *input, uint32_t *codepoint,
                     uint32_t *consumed);
/* Encodes one Unicode scalar value. Returns 0 for an invalid scalar. */
uint32_t utf8_encode_one(uint32_t codepoint, char output[4]);

#endif

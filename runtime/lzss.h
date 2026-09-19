#ifndef KISAKU_LZSS_H
#define KISAKU_LZSS_H
#include <stddef.h>
#include <stdint.h>
/* Decode the complete input; reject truncated references and size mismatches. */
int kawa_lzss(const uint8_t *input, size_t packed, uint8_t *output, size_t size);
#endif

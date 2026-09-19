#ifndef KISAKU_RMT_H
#define KISAKU_RMT_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    int32_t x, y;
    uint32_t width, height;
    size_t stride;
    uint8_t *pixels; /* owned, top-down BGRA8, straight alpha */
} KImage;
/* On failure output is zeroed. Output must not own a previous allocation. */
int rmt_decode(const uint8_t *data, size_t size, KImage *out);
void rmt_free(KImage *image);
#endif

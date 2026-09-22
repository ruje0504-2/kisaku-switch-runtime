#ifndef KISAKU_PRESENT_FILTER_H
#define KISAKU_PRESENT_FILTER_H

#include <stddef.h>
#include <stdint.h>

/* Presentation-only scaler.  The source buffer is never modified.  Pixels
   are BGRA8, matching KImage and SDL_PIXELFORMAT_BGRA32. */
int kpresent_resize_cas(const uint8_t *src, unsigned src_width,
                        unsigned src_height, size_t src_stride,
                        uint8_t *dst, unsigned dst_width,
                        unsigned dst_height, size_t dst_stride,
                        unsigned sharpness_percent);

/* Nearest is kept as a separate path for pixel tests and reference captures. */
int kpresent_resize_nearest(const uint8_t *src, unsigned src_width,
                            unsigned src_height, size_t src_stride,
                            uint8_t *dst, unsigned dst_width,
                            unsigned dst_height, size_t dst_stride);

#endif

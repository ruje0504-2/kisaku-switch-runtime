/* GPL-2.0-or-later. AKB format reference: GARbro, ArcFormats/Silky/ImageAKB.cs
 * Copyright (C) 2015 by morkt (MIT); see THIRD_PARTY.md.
 * All 2191 images in the local Kisaku edition are standalone AKB, not AKB+.
 */
#include "akb.h"
#include "lzss.h"
#include <stdlib.h>
#include <string.h>
static uint32_t u32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
int akb_decode(const uint8_t *d, size_t n, KImage *im) {
    if (!im) return -1;
    memset(im,0,sizeof(*im));
    if (!d || n<32 || memcmp(d,"AKB ",4)) return -1;
    unsigned w=d[4]|(unsigned)d[5]<<8, h=d[6]|(unsigned)d[7]<<8;
    uint32_t flags=u32(d+8), x=u32(d+16), y=u32(d+20);
    uint32_t right=u32(d+24), bottom=u32(d+28);
    if (!w || !h || (uint64_t)w*h>16777216 || x>right || y>bottom || right>w || bottom>h) return -1;
    unsigned channels=(flags&0x40000000)?3:4;
    size_t iw=right-x, ih=bottom-y, stride=iw*channels, bytes=stride*ih;
    uint8_t *packed=malloc(bytes?bytes:1), *out=malloc((size_t)w*h*4);
    if (!packed || !out) { free(packed); free(out); return -1; }
    if (bytes && kawa_lzss(d+32,n-32,packed,bytes)) { free(packed); free(out); return -1; }
    /* AKB stores rows bottom-up, but predicts in top-down order. */
    for (size_t row=0;row<ih/2;row++) for (size_t k=0;k<stride;k++) {
        size_t a=row*stride+k,b=(ih-row-1)*stride+k;
        uint8_t t=packed[a];packed[a]=packed[b];packed[b]=t;
    }
    for (size_t k=channels;k<stride;k++) packed[k]=(uint8_t)(packed[k]+packed[k-channels]);
    for (size_t k=stride;k<bytes;k++) packed[k]=(uint8_t)(packed[k]+packed[k-stride]);
    /* Kisaku 4df9e0: 32-bit input always retains its fourth channel.
       0x80000000 controls filling outside the crop, not alpha presence.
       24-bit input gets the constant alpha stored in the low flag byte. */
    unsigned alpha=channels==4;
    for (size_t k=0;k<(size_t)w*h;k++) {
        memcpy(out+k*4,d+12,3);out[k*4+3]=(flags&0x80000000)?d[15]:(alpha?0:(uint8_t)flags);
    }
    for (size_t row=0;row<ih;row++) for (size_t col=0;col<iw;col++) {
        const uint8_t *p=packed+(row*iw+col)*channels;
        uint8_t *q=out+(((size_t)y+row)*w+x+col)*4;
        memcpy(q,p,3);q[3]=alpha?p[3]:(uint8_t)flags;
    }
    free(packed);
    *im=(KImage){0,0,w,h,(size_t)w*4,out};
    return 0;
}

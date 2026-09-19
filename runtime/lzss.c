#include "lzss.h"
int kawa_lzss(const uint8_t *in, size_t packed, uint8_t *out, size_t size) {
    if ((!in && packed) || (!out && size)) return -1;
    uint8_t ring[4096]={0};
    size_t pos=0, used=0, cursor=4078;
    while (pos<packed) {
        unsigned flags=in[pos++];
        for (unsigned bit=0;bit<8 && pos<packed;bit++) {
            if (flags & (1u<<bit)) {
                if (used>=size) return -1;
                out[used++]=ring[cursor]=in[pos++]; cursor=(cursor+1)&4095;
            } else {
                if (packed-pos<2) return -1;
                unsigned lo=in[pos++], hi=in[pos++];
                unsigned source=lo|((hi&240)<<4), count=(hi&15)+3;
                if (count>size-used) return -1;
                for (unsigned k=0;k<count;k++) {
                    uint8_t value=ring[(source+k)&4095];
                    out[used++]=ring[cursor]=value; cursor=(cursor+1)&4095;
                }
            }
        }
    }
    return used==size ? 0 : -1;
}

/* GPL-2.0-or-later. RMT algorithm reference: morkt/GARbro (MIT),
 * ArcFormats/elf/ImageRMT.cs. Attribution in THIRD_PARTY.md. */
#include "rmt.h"
#include "akb.h"
#include "lzss.h"
#include <stdlib.h>
#include <string.h>
static uint32_t le32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
void rmt_free(KImage *im) { if(im){free(im->pixels);memset(im,0,sizeof(*im));} }
int rmt_decode(const uint8_t *data, size_t size, KImage *im) {
    if(!im)return -1;
    memset(im,0,sizeof(*im));
    if(data&&size>=4&&!memcmp(data,"AKB ",4))return akb_decode(data,size,im);
    if(!data||size<20||memcmp(data,"RMT ",4))return -1;
    uint32_t w=le32(data+12),h=le32(data+16);
    if(!w||!h||(uint64_t)w*h>16777216)return -1;
    size_t stride=(size_t)w*4, bytes=stride*h;
    uint8_t *p=malloc(bytes);
    if(!p)return -1;
    if(kawa_lzss(data+20,size-20,p,bytes)){free(p);return -1;}
    for(size_t i=4;i<stride;i++)p[i]=(uint8_t)(p[i]+p[i-4]);
    for(size_t i=stride;i<bytes;i++)p[i]=(uint8_t)(p[i]+p[i-stride]);
    /* Flip in place; no second image-sized allocation. */
    for(uint32_t row=0;row<h/2;row++) {
        uint8_t *a=p+(size_t)row*stride,*b=p+(size_t)(h-1-row)*stride;
        for(size_t i=0;i<stride;i++){uint8_t tmp=a[i];a[i]=b[i];b[i]=tmp;}
    }
    im->x=(int32_t)le32(data+4);im->y=(int32_t)le32(data+8);
    im->width=w;im->height=h;im->stride=stride;im->pixels=p;return 0;
}

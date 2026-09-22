#include "present_filter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void){
    uint8_t source[4*4*4],before[sizeof(source)],raw[8*8*4],sharp[8*8*4];
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){
        uint8_t *p=source+(y*4+x)*4;
        p[0]=(uint8_t)(x*64);p[1]=(uint8_t)(y*64);p[2]=(uint8_t)((x+y)*32);p[3]=255;
    }
    memcpy(before,source,sizeof(source));
    assert(!kpresent_resize_nearest(source,4,4,16,raw,8,8,32));
    assert(!memcmp(raw,source,4));
    assert(!kpresent_resize_cas(source,4,4,16,sharp,8,8,32,100));
    assert(!memcmp(source,before,sizeof(source)));
    for(unsigned i=0;i<8*8;i++){
        const uint8_t *p=sharp+i*4;
        assert(p[0]<=192&&p[1]<=192&&p[2]<=192&&p[3]==255);
    }
    assert(!kpresent_resize_cas(source,4,4,16,sharp,8,8,32,0));
    assert(!memcmp(raw,sharp,sizeof(raw)));
    /* RCAS' min/max limiter must preserve a flat neighbourhood at every
       strength; this catches a wrong centre coefficient immediately. */
    uint8_t flat[4*4*4],flat_out[8*8*4];
    for(unsigned i=0;i<4*4;i++){flat[i*4+0]=91;flat[i*4+1]=127;flat[i*4+2]=203;flat[i*4+3]=255;}
    assert(!kpresent_resize_cas(flat,4,4,16,flat_out,8,8,32,100));
    for(unsigned i=0;i<8*8;i++)assert(flat_out[i*4+0]==91&&flat_out[i*4+1]==127&&flat_out[i*4+2]==203&&flat_out[i*4+3]==255);
    assert(kpresent_resize_cas(NULL,4,4,16,sharp,8,8,32,50)<0);
    puts("Presentation bilinear/RCAS and raw-nearest paths: PASS");
    return 0;
}

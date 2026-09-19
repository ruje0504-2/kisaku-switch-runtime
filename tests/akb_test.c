#include "akb.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void put32(uint8_t *p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
int main(void){
    /* A 2x2 crop in a 3x3 canvas. Literal-only stream, native bottom-up
       storage, top-down predictors. Expected colors are written explicitly. */
    uint8_t d[46]={0};memcpy(d,"AKB ",4);d[4]=d[6]=3;
    put32(d+8,0x400000ff);d[12]=1;d[13]=2;d[14]=3;
    put32(d+16,1);put32(d+20,1);put32(d+24,3);put32(d+28,3);
    const uint8_t stream[]={255,60,60,60,60,60,60,10,20,15,30,30,30,30};
    memcpy(d+32,stream,sizeof(stream));KImage im={0};
    assert(!akb_decode(d,sizeof(d),&im));
    assert(im.width==3&&im.height==3&&im.x==0&&im.y==0&&im.stride==12);
    const uint8_t expected[]={1,2,3,255, 1,2,3,255, 1,2,3,255,
        1,2,3,255, 10,20,30,255, 40,50,60,255,
        1,2,3,255, 70,80,90,255, 100,110,120,255};
    assert(!memcmp(im.pixels,expected,sizeof(expected)));rmt_free(&im);
    assert(akb_decode(d,sizeof(d)-1,&im));assert(!im.pixels);
    put32(d+16,4);assert(akb_decode(d,sizeof(d),&im));put32(d+16,1);
    d[3]='+';assert(akb_decode(d,sizeof(d),&im));d[3]=' ';
    /* Full BGRA pixel, with and without explicit alpha flag. */
    memset(d,0,sizeof(d));memcpy(d,"AKB ",4);d[4]=d[6]=1;
    put32(d+8,0x80000000);put32(d+24,1);put32(d+28,1);
    d[32]=15;d[33]=9;d[34]=8;d[35]=7;d[36]=6;
    assert(!akb_decode(d,37,&im));assert(im.pixels[3]==6);rmt_free(&im);
    put32(d+8,0);assert(!akb_decode(d,37,&im));assert(im.pixels[3]==255);rmt_free(&im);
    /* Empty crop uses header background, without reading an absent payload. */
    put32(d+24,0);put32(d+28,0);d[12]=42;
    assert(!akb_decode(d,32,&im));assert(im.pixels[0]==42);rmt_free(&im);
    assert(akb_decode(NULL,0,&im));assert(akb_decode(d,32,NULL));
    puts("AKB orientation, crop, alpha, background and malformed input: PASS");
}

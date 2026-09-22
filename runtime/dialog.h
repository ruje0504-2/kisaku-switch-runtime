#ifndef KISAKU_DIALOG_H
#define KISAKU_DIALOG_H
#include "rmt.h"
#include <stdlib.h>
#include <string.h>
/* CDIB+6c -> 48bda0: compose straight BGRA, truncating each term. */
static inline void kdialog_pixel(uint8_t *d,const uint8_t *s){
    unsigned a=s[3],da=d[3];if(!a)return;
    if(a==255||!da){memcpy(d,s,4);return;}
    unsigned out=da+a*(255-da)/255;
    if(da!=255)a=a*255/out;
    for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(d[c]*(255-a)/255+s[c]*a/255);
    d[3]=(uint8_t)out;
}
static inline void kdialog_stamp(KImage *dst,const KImage *src,unsigned dx,unsigned dy,unsigned sx,unsigned sy,unsigned w,unsigned h){
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        kdialog_pixel(dst->pixels+(dy+y)*dst->stride+(dx+x)*4,src->pixels+(sy+y)*src->stride+(sx+x)*4);
}
/* 4bf530 / 4bfaa0: dialog0 quit, dialog1 title, dialog7 scene selection. */
static inline int kdialog_draw(KImage *out,KImage *body,const KImage *base,const KImage *atlas,unsigned kind,int selected){
    if(!out||!body||!base||!atlas||!out->pixels||!base->pixels||!atlas->pixels||(kind!=0&&kind!=1&&kind!=2&&kind!=7)||
       out->width!=640||out->height!=480||base->width<640||base->height<480||atlas->width<428||atlas->height<(kind==7?312u:kind==2?192u:168u))return -1;
    if(!body->pixels){*body=(KImage){0,0,428,88,428*4,calloc(88,428*4)};if(!body->pixels)return -1;}
    if(body->width!=428||body->height!=88)return -1;
    for(unsigned y=0;y<88;y++)memcpy(body->pixels+y*body->stride,atlas->pixels+y*atlas->stride,428*4);
    if(kind==2){kdialog_stamp(body,atlas,76,8,0,168,276,24);kdialog_stamp(body,atlas,124,32,0,136,180,24);}
    else if(kind==7)kdialog_stamp(body,atlas,76,20,0,288,276,24);
    else kdialog_stamp(body,atlas,124,20,0,88+kind*24,180,24);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *s=base->pixels+y*base->stride+x*4;uint8_t *d=out->pixels+y*out->stride+x*4;
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(s[c]*63/255);
        d[3]=255;
    }
    kdialog_stamp(out,body,106,196,0,0,428,88);
    /* Native priority: No first, then Yes over the twelve-pixel overlap. */
    kdialog_stamp(out,atlas,314,256,276,88+(selected==0?20:0),96,20);
    kdialog_stamp(out,atlas,230,256,180,88+(selected==1?20:0),96,20);
    return 0;
}
/* data.arc/dialog.area: native IDs 0=No, 1=Yes; exclusive right/bottom. */
static inline int kdialog_hit(int x,int y){
    if(y<254||y>=274)return -1;
    if(x>=322&&x<412)return 0;
    if(x>=232&&x<322)return 1;
    return -1;
}
#endif

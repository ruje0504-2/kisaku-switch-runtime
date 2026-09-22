#include "present_filter.h"
#include <math.h>
#include <string.h>

static unsigned clamp_u(unsigned value, unsigned limit){return value>limit?limit:value;}
static unsigned sample_index(unsigned p,unsigned out,unsigned in){
    /* Pixel-centre mapping is the same mapping used by a conventional
       fullscreen bilinear sampler and avoids a half-pixel seam at 1.5x. */
    if(!out||!in)return 0;
    unsigned v=(unsigned)(((uint64_t)p*in*2u+in)/(2u*out));
    return clamp_u(v,in-1);
}
static uint8_t bilinear(const uint8_t *src,unsigned w,unsigned h,size_t stride,
                        unsigned x,unsigned y,unsigned channel,unsigned dw,unsigned dh){
    /* Work in 16.16 fixed point to make host and ARM output identical. */
    int64_t fx=(int64_t)(((uint64_t)(x*2u+1u)*w*65536u)/(2u*dw))-32768;
    int64_t fy=(int64_t)(((uint64_t)(y*2u+1u)*h*65536u)/(2u*dh))-32768;
    unsigned x0=fx<0?0:(unsigned)(fx>>16),y0=fy<0?0:(unsigned)(fy>>16);
    if(x0>=w)x0=w-1;
    if(y0>=h)y0=h-1;
    unsigned x1=x0+1<w?x0+1:x0,y1=y0+1<h?y0+1:y0;
    unsigned ax=fx<0?0:(unsigned)fx&65535u,ay=fy<0?0:(unsigned)fy&65535u;
    const uint8_t *a=src+y0*stride+x0*4,*b=src+y0*stride+x1*4;
    const uint8_t *c=src+y1*stride+x0*4,*d=src+y1*stride+x1*4;
    unsigned top=((unsigned)a[channel]*(65536u-ax)+(unsigned)b[channel]*ax+32768u)>>16;
    unsigned bot=((unsigned)c[channel]*(65536u-ax)+(unsigned)d[channel]*ax+32768u)>>16;
    return (uint8_t)((top*(65536u-ay)+bot*ay+32768u)>>16);
}
static uint8_t rcas_channel(const uint8_t *src,unsigned sw,unsigned sh,size_t ss,
                            unsigned x,unsigned y,unsigned channel,unsigned dw,unsigned dh,
                            unsigned sharpness_percent){
    float center=bilinear(src,sw,sh,ss,x,y,channel,dw,dh)/255.0f;
    float mn=center,mx=center,sum=0.0f;
    for(int oy=-1;oy<=1;oy++)for(int ox=-1;ox<=1;ox++){
        int nx=(int)x+ox,ny=(int)y+oy;if(nx<0)nx=0;if(ny<0)ny=0;
        if(nx>=(int)dw)nx=(int)dw-1;
        if(ny>=(int)dh)ny=(int)dh-1;
        float value=bilinear(src,sw,sh,ss,(unsigned)nx,(unsigned)ny,channel,dw,dh)/255.0f;
        if(value<mn)mn=value;
        if(value>mx)mx=value;
        if((ox==0)^(oy==0))sum+=value; /* four cardinal neighbours */
    }
    /* RCAS's limiting envelope and square-root amplification, matching the
       scalar form in ffx_cas.h.  The authored strength maps linearly to the
       [-1/5,0] peak and is never allowed to alter source surfaces. */
    float envelope=mn<2.0f-mx?mn:2.0f-mx;
    if(envelope<0.0f)envelope=0.0f;
    float denom=mx>1.0e-4f?mx:1.0e-4f;
    float amp=envelope/denom;amp=amp<0.0f?0.0f:amp>1.0f?1.0f:amp;
    amp=sqrtf(amp>1.0e-4f?amp:1.0e-4f);
    float weight=-0.2f*((float)sharpness_percent/100.0f)*amp;
    float result=(center+weight*sum)/(1.0f+4.0f*weight);
    if(result<mn)result=mn;
    if(result>mx)result=mx;
    if(result<0.0f)result=0.0f;
    if(result>1.0f)result=1.0f;
    return (uint8_t)(result*255.0f+0.5f);
}
int kpresent_resize_nearest(const uint8_t *src,unsigned sw,unsigned sh,size_t ss,
                            uint8_t *dst,unsigned dw,unsigned dh,size_t ds){
    if(!src||!dst||!sw||!sh||!dw||!dh||ss<(size_t)sw*4||ds<(size_t)dw*4)return -1;
    for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
        const uint8_t *p=src+sample_index(y,dh,sh)*ss+sample_index(x,dw,sw)*4;
        memcpy(dst+y*ds+x*4,p,4);
    }
    return 0;
}
int kpresent_resize_bilinear(const uint8_t *src,unsigned sw,unsigned sh,size_t ss,
                             uint8_t *dst,unsigned dw,unsigned dh,size_t ds){
    if(!src||!dst||!sw||!sh||!dw||!dh||ss<(size_t)sw*4||ds<(size_t)dw*4)return -1;
    for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
        uint8_t *out=dst+y*ds+x*4;
        for(unsigned c=0;c<4;c++)out[c]=bilinear(src,sw,sh,ss,x,y,c,dw,dh);
    }
    return 0;
}
int kpresent_resize_cas(const uint8_t *src,unsigned sw,unsigned sh,size_t ss,
                        uint8_t *dst,unsigned dw,unsigned dh,size_t ds,
                        unsigned sharpness_percent){
    if(!src||!dst||!sw||!sh||!dw||!dh||ss<(size_t)sw*4||ds<(size_t)dw*4)return -1;
    if(sharpness_percent>100)sharpness_percent=100;
    /* RCAS sharpness is mapped to [0, 1/5].  Keeping the negative neighbour
       coefficient explicit makes the clamp and the reference formula easy to
       audit against FidelityFX's ffx_cas.h. */
    if(!sharpness_percent)return kpresent_resize_bilinear(src,sw,sh,ss,dst,dw,dh,ds);
    for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
        uint8_t out[4];
        for(unsigned c=0;c<3;c++)out[c]=rcas_channel(src,sw,sh,ss,x,y,c,dw,dh,sharpness_percent);
        out[3]=bilinear(src,sw,sh,ss,x,y,3,dw,dh);memcpy(dst+y*ds+x*4,out,4);
    }
    return 0;
}

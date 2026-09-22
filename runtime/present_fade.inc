/* Presentation-only transitions. Save the clean endpoint and rerasterized
   glyphs before native clear/exit destroys its text surface. */
static int present_fade_prepare(KBootstrap *b){
    return present_surface(&b->present_fade_to,640,480)||present_surface(&b->present_fade_from,640,480)||
        present_surface(&b->present_fade_text,960,720)||
        present_surface(&b->present_fade_base,640,480)||
        present_surface(&b->present_fade_reference,640,480)||
        present_surface(&b->present_fade_overlay,960,720);
}
static void present_fade_capture(KBootstrap *b){
    b->present_fade_valid=b->present_fade_active=0;
    if(!b->present_hires||!b->present_page_active||!present_page_matches(b)||present_fade_prepare(b))return;
    memcpy(b->present_fade_from.pixels,b->present_page_base.pixels,640*480*4);
    memcpy(b->present_fade_text.pixels,b->present_page_text.pixels,960*720*4);
    b->present_fade_valid=1;
}
/* Pixel-center bilinear sampling, exact rational 3:2 coordinates. Only
   glyph-covered pixels use this; the rest stays on the GPU upscale path. */
static unsigned present_fade_sample(const KImage *im,unsigned x,unsigned y,unsigned c){
    int sx=(int)x*4-1,sy=(int)y*4-1;
    if(sx<0)sx=0;
    if(sy<0)sy=0;
    unsigned x0=(unsigned)sx/6,y0=(unsigned)sy/6,fx=(unsigned)sx%6,fy=(unsigned)sy%6;
    unsigned x1=x0<639?x0+1:x0,y1=y0<479?y0+1:y0;
    const uint8_t *a=im->pixels+y0*im->stride,*d=im->pixels+y1*im->stride;
    return (a[x0*4+c]*(6-fx)*(6-fy)+a[x1*4+c]*fx*(6-fy)+
        d[x0*4+c]*(6-fx)*fy+d[x1*4+c]*fx*fy+18)/36;
}
static void present_fade_frame(KBootstrap *b,const KImage *to,unsigned alpha,int incoming){
    b->present_fade_active=0;
    if(!b->present_hires||alpha>=255)return;
    if(incoming){
        if(!present_page_matches(b)||present_fade_prepare(b))return;
    }else if(!b->present_fade_valid)return;
    for(unsigned y=0;y<480;y++){
        memcpy(b->present_fade_to.pixels+y*2560,to->pixels+y*to->stride,2560);
        memcpy(b->present_fade_reference.pixels+y*2560,b->layers[0].pixels+y*b->layers[0].stride,2560);
    }
    b->present_fade_alpha=alpha;b->present_fade_incoming=(unsigned)incoming;b->present_fade_active=1;
}
/* Called by the presentation worker; VM/frame work only captures endpoints. */
static void present_fade_generate(KBootstrap *b){
    unsigned alpha=b->present_fade_alpha,incoming=b->present_fade_incoming;
    const KImage *from=&b->present_fade_from,*to=&b->present_fade_to;
    const KImage *glyphs=incoming?&b->present_page_source:&b->present_fade_text;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *s=from->pixels+y*from->stride+x*4,*t=to->pixels+y*to->stride+x*4;
        uint8_t *d=b->present_fade_base.pixels+y*2560+x*4;
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(s[c]*(255-alpha)/255+t[c]*alpha/255);
        d[3]=s[3];
    }
    memset(b->present_fade_overlay.pixels,0,960*720*4);
    for(unsigned y=0;y<720;y++)for(unsigned x=0;x<960;x++){
        size_t i=((size_t)y*960+x)*4;const uint8_t *s=glyphs->pixels+i;
        if(!s[3])continue;
        uint8_t *d=b->present_fade_overlay.pixels+i;
        /* Blend the two complete endpoints, not just glyph opacity over an
           interpolated background (which creates halos on darkening fades).
           Opaque coverage patches avoid multiplying glyph alpha twice. */
        for(unsigned c=0;c<3;c++){
            unsigned a=present_fade_sample(from,x,y,c),z=present_fade_sample(to,x,y,c);
            if(incoming)z=z*(255-s[3])/255+s[c]*s[3]/255;
            else a=a*(255-s[3])/255+s[c]*s[3]/255;
            d[c]=(uint8_t)(a*(255-alpha)/255+z*alpha/255);
        }
        d[3]=255;
    }
}
static void present_fade_release(KBootstrap *b){
    b->present_fade_valid=b->present_fade_active=0;
    rmt_free(&b->present_fade_to);rmt_free(&b->present_fade_from);rmt_free(&b->present_fade_text);
    rmt_free(&b->present_fade_base);rmt_free(&b->present_fade_reference);rmt_free(&b->present_fade_overlay);
}
static int present_mes_matches(KBootstrap *b,const KImage *src){
    if(!b->present_hires||!b->present_mes_valid||!src->pixels||src->width!=640||src->height<480)return 0;
    for(unsigned y=0;y<480;y++)if(memcmp(src->pixels+y*src->stride,b->present_mes_shadow.pixels+y*2560,2560)){
        b->present_mes_valid=0;return 0;
    }
    return 1;
}
static void present_mes_reset(KBootstrap *b,const KImage *src){
    b->present_mes_valid=0;
    if(!b->present_hires||!src->pixels||src->width!=640||src->height<480||
       present_surface(&b->present_mes_source,960,720)||present_surface(&b->present_mes_shadow,640,480)||
       present_surface(&b->present_mes_old,960,720)||present_surface(&b->present_mes_new,960,720))return;
    memset(b->present_mes_source.pixels,0,960*720*4);
    memset(b->present_mes_old.pixels,0,960*720*4);memset(b->present_mes_new.pixels,0,960*720*4);
    for(unsigned y=0;y<480;y++)memcpy(b->present_mes_shadow.pixels+y*2560,src->pixels+y*src->stride,2560);
    b->present_mes_valid=1;
}

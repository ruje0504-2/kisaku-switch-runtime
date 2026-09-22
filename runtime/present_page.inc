/* Presentation companions for the full-page text modes. Native surfaces
   remain authoritative for scripts, saves and the raw-nearest path. */
static void present_page_reset(KBootstrap *b){
    b->present_page_valid=b->present_page_active=0;
    if(!b->present_hires)return;
    if(present_surface(&b->present_page_source,960,720)||
       present_surface(&b->present_page_text,960,720)||
       present_surface(&b->present_page_shadow,640,480)||
       present_surface(&b->present_page_base,640,480)||
       present_surface(&b->present_page_reference,640,480))return;
    memset(b->present_page_source.pixels,0,960*720*4);
    memset(b->present_page_text.pixels,0,960*720*4);
    memset(b->present_page_shadow.pixels,0,640*480*4);
    b->present_page_valid=1;
}
static int present_page_matches(KBootstrap *b){
    KImage *text=&b->layers[1];
    if(!b->present_hires||!b->present_page_valid||!text->pixels||text->width!=640||text->height<480)return 0;
    for(unsigned y=0;y<480;y++)if(memcmp(text->pixels+y*text->stride,b->present_page_shadow.pixels+y*2560,2560)){
        b->present_page_valid=b->present_page_active=0;return 0;
    }
    return 1;
}
static void present_page_background(KBootstrap *b,const KImage *base){
    if(!b->present_page_valid)return;
    for(unsigned y=0;y<480;y++)memcpy(b->present_page_base.pixels+y*2560,base->pixels+y*base->stride,2560);
}
static void present_page_snapshot(KBootstrap *b){
    if(!present_page_matches(b))return;
    for(unsigned y=0;y<480;y++)memcpy(b->present_page_reference.pixels+y*2560,b->layers[0].pixels+y*b->layers[0].stride,2560);
    b->present_page_active=1;
}
static void present_page_all(KBootstrap *b){
    if(!present_page_matches(b))return;
    memcpy(b->present_page_text.pixels,b->present_page_source.pixels,960*720*4);
    present_page_snapshot(b);
}
/* Use the native mask at its original coordinate, but apply the ramp to
   newly rasterized coverage, not to an enlarged low-resolution glyph. */
static void present_page_line(KBootstrap *b,int x,int y,int height,const uint8_t *mask,int phase,int scale){
    if(!b->present_page_valid||x<0||x>640||y<0||height<0||y+height>480)return;
    for(int yy=(y*3+1)/2;yy<((y+height)*3+1)/2;yy++)for(int xx=(x*3+1)/2;xx<960;xx++){
        size_t i=((size_t)yy*960+xx)*4;
        const uint8_t *s=b->present_page_source.pixels+i;uint8_t *d=b->present_page_text.pixels+i;
        memcpy(d,s,4);
        if(mask){
            int ramp=phase-mask[(yy*2/3)*640+xx*2/3];
            if(ramp<0)ramp=0;
            if(ramp>scale)ramp=scale;
            unsigned a=(unsigned)ramp*255/(unsigned)scale;
            if(a<d[3])d[3]=(uint8_t)a;
        }
    }
}
static void present_page_free(KBootstrap *b){
    b->present_page_valid=b->present_page_active=0;
    rmt_free(&b->present_page_source);rmt_free(&b->present_page_text);
    rmt_free(&b->present_page_shadow);rmt_free(&b->present_page_base);rmt_free(&b->present_page_reference);
}

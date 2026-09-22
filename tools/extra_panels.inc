/* AKB artwork carries real alpha (including the green transparent surround
   in the music atlas), so forcing alpha to 255 would recreate the old
   green-block artifact. */
static void appendix_blit(KImage *dst,const KImage *src,int sx,int sy,int sw,int sh,int dx,int dy){
    if(!dst||!dst->pixels||!src||!src->pixels)return;
    for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
        if(sx+x<0||sy+y<0||(unsigned)(sx+x)>=src->width||(unsigned)(sy+y)>=src->height||dx+x<0||dy+y<0||(unsigned)(dx+x)>=dst->width||(unsigned)(dy+y)>=dst->height)continue;
        const uint8_t *s=src->pixels+(sy+y)*src->stride+(sx+x)*4;unsigned a=s[3];if(!a)continue;
        uint8_t *d=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
        if(a==255){memcpy(d,s,4);continue;}
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)((s[c]*a+d[c]*(255-a))/255);
        d[3]=255;
    }
}
static void appendix_crop_over(KImage *dst,const KImage *src,int sx,int sy,int sw,int sh,int dx,int dy,int dw,int dh){
    if(!dst||!dst->pixels||!src||!src->pixels||sw<=0||sh<=0||dw<=0||dh<=0)return;
    for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){
        unsigned xx=(unsigned)((uint64_t)x*(unsigned)sw/(unsigned)dw),yy=(unsigned)((uint64_t)y*(unsigned)sh/(unsigned)dh);
        int px=sx+(int)xx,py=sy+(int)yy;if(px<0||py<0||(unsigned)px>=src->width||(unsigned)py>=src->height||dx+x<0||dy+y<0||(unsigned)(dx+x)>=dst->width||(unsigned)(dy+y)>=dst->height)continue;
        const uint8_t *s=src->pixels+py*src->stride+px*4;unsigned a=s[3];if(!a)continue;uint8_t *d=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
        if(a==255){memcpy(d,s,4);continue;}
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)((s[c]*a+d[c]*(255-a))/255);
        d[3]=255;
    }
}
static void appendix_rect(KImage *im,int x,int y,int w,int h,unsigned rgb){
    if(!im||!im->pixels)return;
    ui_fill(im,x,y,w,1,rgb);ui_fill(im,x,y+h-1,w,1,rgb);
    ui_fill(im,x,y,1,h,rgb);ui_fill(im,x+w-1,y,1,h,rgb);
}
static int appendix_assets(MessagePanel *p,KBootstrap *b,unsigned video){
    const char *base=video?"kisaku_DL_video_p.akb":"kisaku_DL_music_p.akb";
    /* A single runtime panel is reused when the title switches between
       music and video.  Do not mistake the 714px music atlas for the 640px
       video background (or retain its p2 state cells). */
    if((video&&p->appendix_artwork.pixels&&p->appendix_artwork.width!=640)||
       (!video&&p->appendix_artwork.pixels&&p->appendix_artwork.width!=714)){
        rmt_free(&p->appendix_artwork);rmt_free(&p->appendix_parts);
    }
    if(!p->appendix_artwork.pixels&&ui_akb_asset(b,base,&p->appendix_artwork))return -1;
    if(video&&!p->appendix_parts.pixels&&ui_akb_asset(b,"kisaku_DL_video_p2.akb",&p->appendix_parts))return -1;
    return 0;
}
static int music_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    if(appendix_assets(p,b,0)||ui_canvas(&p->image))return -1;
    ui_scale(&p->image,&b->layers[0],0,0,640,480);
    /* CMusicMode::virtual_12 builds a compact list of the unlocked tracks;
       virtual_0 then draws each entry at (0x9c + 10*i, 0x38 + 0x24*i),
       copying a 0xee x 0x20 cell from the three-column atlas.  The atlas is
       four 32px state rows per 0x80px group (normal, hover, playing, locked),
       with the state selected by the mode's cursor/playing slot.  The old
       preview treated the atlas as a 16-row 540px grid, which sliced through
       glyphs and exposed the wrong Japanese labels on Switch.
       Keep the native overlapping list geometry and source stride exactly. */
    unsigned count=bootstrap_music_count(),visible=0;
    for(unsigned i=0;i<count;i++)if(bootstrap_music_unlocked(b,i)){
        unsigned state=b->music_active&&!SDL_strcasecmp(b->audio_name,bootstrap_music_name(i))?2:p->selected==i?1:0;
        int sx=(int)(i%3)*238,sy=(int)(i/3)*128+28+(int)state*32;
        appendix_crop_over(&p->image,&p->appendix_artwork,sx,sy,238,32,
                           156+(int)visible*10,56+(int)visible*36,238,32);
        visible++;
    }
    /* 4814e0's final two copies use the dedicated stop/back cells rather than
       the track rows: stop is (0xee,0x1fc,0x82,0x20), back is
       (0x170,0x19c,0x70,0x20). */
    appendix_crop_over(&p->image,&p->appendix_artwork,238,508,130,32,418,448,130,32);
    appendix_crop_over(&p->image,&p->appendix_artwork,368,412,112,32,528,448,112,32);
    if(p->status[0])ui_text(b,&p->image,p->status,16,446,608,22,0xffd070,0);
    ui_present(r,&p->image,&p->texture);return 0;
}
/* 422050 / 422220: original animation-versus-still modal atlas. */
static int style_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    if(p->cg_cached!=3){SDL_Texture *texture=gallery_panel_texture(b,r,"sl_data2.rmt",504,68,2);if(!texture)return -1;gallery_panel_clear(p);p->cg_textures[0]=texture;p->cg_cached=3;}
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,0,0,0,128);SDL_Rect dim={160,0,960,720};SDL_RenderFillRect(r,&dim);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    gallery_panel_copy(r,p->cg_textures[0],0,0,288,68,180,208);
    for(unsigned i=0;i<2;i++)gallery_panel_copy(r,p->cg_textures[0],288+(p->selected==i?72:0),i*28,72,28,236+i*104,236);
    SDL_SetRenderDrawColor(r,245,245,245,255);return 0;
}

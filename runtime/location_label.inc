/* 31/526/2 -> 47b380: location label, two private sprites (priorities 10/11).
   Layer 7 is the native scratch/skin atlas; it is not the displayed sprite. */
static void exec526_restore(KBootstrap *b){
    if(!b->exec526_drawn)return;
    KImage *screen=&b->layers[0];
    unsigned width=b->exec526_width,height=b->exec526_height;
    if(b->exec526_x<0||b->exec526_y<0||(unsigned)b->exec526_x>=screen->width||
       (unsigned)b->exec526_y+height>screen->height)return;
    /* The original location-label path keeps a 336px backing surface while
       clipping its right edge against the 640px screen. */
    if(width>screen->width-(unsigned)b->exec526_x)width=screen->width-(unsigned)b->exec526_x;
    for(unsigned y=0;y<height;y++)memcpy(screen->pixels+(b->exec526_y+y)*screen->stride+b->exec526_x*4,
        b->exec526_backing.pixels+y*b->exec526_backing.stride,width*4);
    b->exec526_drawn=0;
}
static void exec526_present(KBootstrap *b){
    if(!b->exec526_active||b->exec526_drawn)return;
    KImage *screen=&b->layers[0];
    unsigned width=b->exec526_width,height=b->exec526_height;
    if(b->exec526_x<0||b->exec526_y<0||(unsigned)b->exec526_x>=screen->width||
       (unsigned)b->exec526_y+height>screen->height)return;
    if(width>screen->width-(unsigned)b->exec526_x)width=screen->width-(unsigned)b->exec526_x;
    for(unsigned y=0;y<height;y++)memcpy(b->exec526_backing.pixels+y*b->exec526_backing.stride,
        screen->pixels+(b->exec526_y+y)*screen->stride+b->exec526_x*4,width*4);
    for(unsigned i=0;i<2;i++){
        KImage *sprite=&b->exec526_surfaces[i];
        for(unsigned y=0;y<sprite->height;y++)for(unsigned x=0;x<sprite->width;x++){
            int dx=b->exec526_x+(int)x,dy=b->exec526_y+(int)y;
            if(dx<0||dy<0||dx>=640||dy>=480)continue;
            const uint8_t *s=sprite->pixels+y*sprite->stride+x*4;
            uint8_t *d=screen->pixels+dy*screen->stride+dx*4;unsigned alpha=s[3];
            if(alpha)for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(d[c]*(255-alpha)/255+s[c]*alpha/255);
        }
    }
    b->exec526_drawn=1;
}
static void exec526_release(KBootstrap *b){
    exec526_restore(b);
    for(unsigned i=0;i<2;i++)rmt_free(&b->exec526_surfaces[i]);
    rmt_free(&b->exec526_backing);b->exec526_width=b->exec526_height=0;b->exec526_active=0;
}
/* 31/526/0 -> 47b1c0/47b220/47b020.  The native object creates two
   same-sized sprites, copies a color-keyed rectangle from layer 7 into the
   first one, and presents it at the right-aligned position derived from the
   fifth value.  The second sprite is retained separately for the native
   text/background pair even when its pixels are transparent in this path. */
static int exec526_create(KBootstrap *b,int src_x,int src_y,int width,int height,int right_bytes){
    KImage *atlas=&b->layers[7],*screen=&b->layers[0];
    if(!atlas->pixels||!screen->pixels||src_x<0||src_y<0||width<=0||height<=0||width>640||height>480||
       (uint64_t)src_x+(unsigned)width>atlas->width||(uint64_t)src_y+(unsigned)height>atlas->height||
       right_bytes<0||right_bytes>76)return error(b,"31/526 create rectangle invalid (arguments preserved)");
    int x=608-right_bytes*8,y=16;
    if(x<0||(unsigned)x+(unsigned)width>screen->width||(unsigned)y+(unsigned)height>screen->height)
        return error(b,"31/526 create destination invalid (arguments preserved)");
    KImage pair[2]={{0}},back={0};
    for(unsigned i=0;i<2;i++)pair[i]=(KImage){0,0,(unsigned)width,(unsigned)height,(size_t)width*4,calloc((size_t)width*height,4)};
    back=(KImage){0,0,(unsigned)width,(unsigned)height,(size_t)width*4,calloc((size_t)width*height,4)};
    if(!pair[0].pixels||!pair[1].pixels||!back.pixels){for(unsigned i=0;i<2;i++)rmt_free(&pair[i]);rmt_free(&back);return error(b,"31/526 create surface allocation failed (arguments preserved)");}
    for(int y0=0;y0<height;y0++)for(int x0=0;x0<width;x0++){
        const uint8_t *s=atlas->pixels+(src_y+y0)*atlas->stride+(src_x+x0)*4;uint8_t *d=pair[0].pixels+y0*pair[0].stride+x0*4;
        if(s[0]==0&&s[1]==255&&s[2]==0)continue;
        memcpy(d,s,4);
    }
    exec526_release(b);b->exec526_surfaces[0]=pair[0];b->exec526_surfaces[1]=pair[1];b->exec526_backing=back;
    b->exec526_x=x;b->exec526_y=y;b->exec526_width=(unsigned)width;b->exec526_height=(unsigned)height;b->exec526_active=1;b->exec526_drawn=0;
    return 0;
}
static int exec526_location(KBootstrap *b){
    KVM *v=b->vm;int place=v->globals[0][0].number;
    KImage *atlas=&b->layers[7],*screen=&b->layers[0];
    if(place<0||(unsigned)place>=sizeof(kisaku_location_names)/sizeof(kisaku_location_names[0]))
        return error(b,"31/526 location index invalid (arguments preserved)");
    if(!atlas->pixels||atlas->width<512||atlas->height<368||!screen->pixels||screen->width!=640||screen->height!=480)
        return error(b,"31/526 location surfaces missing (arguments preserved)");
    const unsigned char *name=kisaku_location_names[place];size_t length=strlen((const char *)name),count=0;
    KTextChar chars[32];KTextPosition positions[32];int x=168,y=344;
    int advance=v->globals[0][30].number/2,line=v->globals[0][31].number;
    if(advance<1||advance>128||line<1||line>256||ktext_decode(KTEXT_CP932,name,length,chars,32,&count)||
       ktext_layout_native(chars,count,168,344,639,479,advance,line,&x,&y,positions))
        return error(b,"31/526 location text layout invalid (arguments preserved)");
    KFont *font=bootstrap_ui_font(b);
    if(!font)return error(b,"31/526 location font missing (arguments preserved)");
    KImage pair[2]={{0}},backing={0};
    for(unsigned i=0;i<2;i++)pair[i]=(KImage){0,0,336,32,1344,calloc(32,1344)};
    backing=(KImage){0,0,336,32,1344,calloc(32,1344)};
    if(!pair[0].pixels||!pair[1].pixels||!backing.pixels)goto failed;
    /* Render at local coordinates, preserving the native 8px inset. */
    for(size_t i=0;i<count;i++)if(kfont_draw(font,&pair[1],chars[i].codepoint,positions[i].x-160,positions[i].y-336,
        (unsigned)b->font_width,(unsigned)b->font_height,(uint32_t)v->globals[0][33].number))goto failed;
    unsigned visible=(unsigned)length*8+16;
    for(unsigned row=0;row<32;row++)memset(pair[1].pixels+row*1344+visible*4,0,(336-visible)*4);
    uint8_t color[4];KImage pixel={0,0,1,1,4,color};message_skin_color(b,&pixel);
    for(unsigned row=0;row<32;row++)for(unsigned col=0;col<visible;col++){
        unsigned sx=col<8?160+col:col>=visible-8?184+col-(visible-8):168+(col-8)%16;
        const uint8_t *s=atlas->pixels+(304+row)*atlas->stride+sx*4;
        uint8_t *d=pair[0].pixels+row*1344+col*4;
        if(s[0]==0&&s[1]==255&&s[2]==0)continue;
        if(s[0]==0&&s[1]==0&&s[2]==255)memcpy(d,color,4);else memcpy(d,s,4);
    }
    exec526_release(b);memcpy(b->exec526_surfaces,pair,sizeof(pair));b->exec526_backing=backing;
    b->exec526_x=608-(int)length*8;b->exec526_y=16;b->exec526_width=336;b->exec526_height=32;b->exec526_active=1;
    /* 47b020 leaves the constructed background in the shared scratch rect. */
    for(unsigned row=0;row<32;row++){
        uint8_t *d=atlas->pixels+(336+row)*atlas->stride+160*4;
        memset(d,0,352*4);
        for(unsigned col=0;col<visible;col++){
            unsigned sx=col<8?160+col:col>=visible-8?184+col-(visible-8):168+(col-8)%16;
            const uint8_t *s=atlas->pixels+(304+row)*atlas->stride+sx*4;
            if(s[0]==0&&s[1]==0&&s[2]==255)memcpy(d+col*4,color,4);else memcpy(d+col*4,s,4);
        }
    }
    v->globals[0][42]=(KValue){168,NULL};v->globals[0][43]=(KValue){344,NULL};
    v->globals[0][44]=(KValue){639,NULL};v->globals[0][45]=(KValue){479,NULL};
    v->globals[0][46]=(KValue){x,NULL};v->globals[0][47]=(KValue){y,NULL};v->globals[0][49]=(KValue){7,NULL};
    return 0;
failed:
    for(unsigned i=0;i<2;i++)rmt_free(&pair[i]);
    rmt_free(&backing);
    return error(b,"31/526 location sprite allocation/render failed (arguments preserved)");
}

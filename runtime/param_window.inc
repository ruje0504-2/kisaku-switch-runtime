/* 49e110/49dee0/49dc60 use CDIB +0x48 with copy-alpha=1.
   The small glyphs are full BGRA copies, not color-keyed sprites. */
static void param_small_digit(KImage *surface,const KImage *atlas,unsigned dx,unsigned glyph){
    unsigned sx=glyph<10?524+glyph*8:524+(glyph-10)*8,sy=glyph<10?112:128;
    for(unsigned y=0;y<16;y++)memcpy(surface->pixels+(92+y)*surface->stride+dx*4,
        atlas->pixels+(sy+y)*atlas->stride+sx*4,8*4);
}
static void param_fourth(KBootstrap *b,KImage *surface,const KImage *atlas){
    /* 49e110: this+0x2e, x=166, y=92.  The native routine intentionally
       does not overwrite the tens cell when the value is below ten. */
    unsigned value=(unsigned)b->param_values[3];
    if(value/10)param_small_digit(surface,atlas,166,value/10);
    param_small_digit(surface,atlas,174,value%10);

    /* 49dee0: this+0x64, x=474, y=92.  Glyph 10 is the native >=100
       fallback and glyph 11 is the native blank tens cell for 0..9. */
    unsigned total=b->param_total,left=b->param_remaining;
    param_small_digit(surface,atlas,474,total>=100?10:total<10?11:total/10);
    param_small_digit(surface,atlas,482,total>=100?10:total%10);

    /* 49dc60: this+0x66, x=534, y=92.  A zero remaining count writes only
       the second cell with glyph 12; the first cell is intentionally left
       as the native surface left it. */
    if(!left)param_small_digit(surface,atlas,542,12);
    else{
        param_small_digit(surface,atlas,534,left>=100?10:left<10?11:left/10);
        param_small_digit(surface,atlas,542,left>=100?10:left%10);
    }
}
/* CKisakuParamWnd 4fc080/10/0 -> 49fc40 (49e250 redraw).
   The first three rows use bars and chroma-keyed decimal digits. */
static int param_window_rows(KBootstrap *b,int rows,int force){
    if(rows<1&&force!=2)rows=1;
    if(rows>4)rows=4;
    if(b->param_values[3]<0||b->param_values[3]>80)return error(b,"CKisakuParamWnd fourth value bounds (arguments preserved)");
    if(b->param_markers[0]<0||b->param_markers[0]>8||b->param_markers[1]<0||b->param_markers[1]>6)return error(b,"CKisakuParamWnd marker bounds (arguments preserved)");
    for(unsigned i=0;i<3;i++)if(b->param_values[i]<0||b->param_values[i]>999)return error(b,"CKisakuParamWnd value bounds (arguments preserved)");
    if(rows==b->param_rows&&!force)return 0;
    KImage atlas={0},surface={0};uint8_t *data=NULL;size_t size=0;
    if(read_named(&b->images,"param.akb",&data,&size)||rmt_decode(data,size,&atlas)){
        free(data);return error(b,"CKisakuParamWnd atlas decode failed (arguments preserved)");
    }
    free(data);
    if(atlas.width<604||atlas.height<156){rmt_free(&atlas);return error(b,"CKisakuParamWnd atlas bounds (arguments preserved)");}
    /* 49ff00 allocates 604 x 260 before loading the 604 x 160 AKB;
       the lower rows are scratch storage, not part of the file. */
    uint8_t *expanded=calloc(260,604*4);
    if(!expanded){rmt_free(&atlas);return error(b,"CKisakuParamWnd scratch allocation failed (arguments preserved)");}
    for(unsigned y=0;y<atlas.height&&y<260;y++)memcpy(expanded+y*604*4,atlas.pixels+y*atlas.stride,604*4);
    rmt_free(&atlas);atlas=(KImage){0,0,604,260,604*4,expanded};
    unsigned height=rows==4?112:rows?30+29*(rows-1):0;
    surface=(KImage){0,0,602,112,602*4,calloc(112,602*4)};
    if(!surface.pixels){rmt_free(&atlas);return error(b,"CKisakuParamWnd allocation failed (arguments preserved)");}
    if(b->param_surface.pixels)for(unsigned y=0;y<b->param_surface.height&&y<112;y++)
        memcpy(surface.pixels+y*surface.stride,b->param_surface.pixels+y*b->param_surface.stride,602*4);
    if(force!=2)for(unsigned y=0;y<height;y++)memcpy(surface.pixels+y*surface.stride,atlas.pixels+y*atlas.stride,602*4);
    int redraw=force==2?3:rows<3?rows:3;
    for(int row=0;row<redraw;row++){
        if(force==2)for(unsigned y=0;y<23;y++)memcpy(surface.pixels+(3+row*29+y)*surface.stride+90*4,
            atlas.pixels+(112+y)*atlas.stride,500*4);
        unsigned value=(unsigned)b->param_values[row],bar=(value+1)/2,dy=11+29*row;
        for(unsigned x=0;x<bar;x++)for(unsigned y=0;y<8;y++)
            memcpy(surface.pixels+(dy+y)*surface.stride+(90+x)*4,atlas.pixels+(112+y)*atlas.stride+(501+row*2)*4,4);
        if(row>0&&b->param_markers[row-1]){
            static const unsigned marker_x[2][8]={{183,233,283,333,383,433,483,533},{133,208,283,383,483,580}};
            unsigned mx=marker_x[row-1][b->param_markers[row-1]-1],my=row==1?33:62;
            for(unsigned y=0;y<7;y++)for(unsigned x=0;x<14;x++){
                const uint8_t *src=atlas.pixels+(112+y)*atlas.stride+(508+x)*4;
                if(src[0]==0&&src[1]==255&&src[2]==0)continue;
                memcpy(surface.pixels+(my+y)*surface.stride+(mx+x)*4,src,4);
            }
        }
        unsigned offset=bar>30?bar-30:0;if(offset>476)offset=476;
        unsigned x0=90+offset,y0=8+29*row;
        /* 49d9c0 saves the pixels behind digits into the mutable atlas. */
        for(unsigned y=0;y<15;y++)memcpy(atlas.pixels+(180+row*15+y)*atlas.stride,
            surface.pixels+(y0+y)*surface.stride+x0*4,27*4);
        unsigned digits[3]={value/100,value/10%10,value%10};
        for(unsigned digit=0;digit<3;digit++)for(unsigned y=0;y<15;y++)for(unsigned x=0;x<9;x++){
            const uint8_t *src=atlas.pixels+(141+y)*atlas.stride+(digits[digit]*9+x)*4;
            if(src[0]==0&&src[1]==255&&src[2]==0)continue;
            memcpy(surface.pixels+(y0+y)*surface.stride+(x0+digit*9+x)*4,src,4);
        }
    }
    if(rows==4||force==2)param_fourth(b,&surface,&atlas);
    rmt_free(&b->param_surface);rmt_free(&b->param_atlas);b->param_surface=surface;b->param_atlas=atlas;b->param_rows=rows;
    return 0;
}

/* 49ebb0 / 49eb60; coordinate arrays are initialized by 5134d0/5135e0. */
static int param_window_marker(KBootstrap *b,int marker,int value){
    if(value<0||value>(marker?6:8))return error(b,"CKisakuParamWnd marker bounds (arguments preserved)");
    int16_t previous=b->param_markers[marker];b->param_markers[marker]=(int16_t)value;
    if(b->param_rows&&param_window_rows(b,b->param_rows,1)){
        b->param_markers[marker]=previous;return -1;
    }
    return 0;
}

/* 4fc080 action 0 -> four consecutive 49f5a0 signed-short assignments. */
static int param_window_values(KBootstrap *b,const int32_t values[4]){
    int16_t previous[4];memcpy(previous,b->param_values,sizeof(previous));
    for(unsigned i=0;i<4;i++){
        int value=(int16_t)(uint16_t)values[i];
        if(value<0)return error(b,"CKisakuParamWnd negative value (arguments preserved)");
    }
    for(unsigned i=0;i<4;i++){
        int value=(int16_t)(uint16_t)values[i],maximum=i==3?80:999;
        b->param_values[i]=(int16_t)(value>maximum?maximum:value);
    }
    if(param_window_rows(b,b->param_rows,2)){memcpy(b->param_values,previous,sizeof(previous));return -1;}
    return 0;
}
/* 49fbe0 stores two shorts; remaining > 99 sets total to the sentinel 255. */
static int param_window_counts(KBootstrap *b,int total,int left){
    uint16_t old_total=b->param_total,old_left=b->param_remaining;
    b->param_total=(uint16_t)total;b->param_remaining=(uint16_t)left;
    if((int16_t)b->param_remaining>99)b->param_total=255;
    if(b->param_rows&&param_window_rows(b,b->param_rows,1)){
        b->param_total=old_total;b->param_remaining=old_left;return -1;
    }
    return 0;
}

/* 474500 reconstructs CKisakuParamWnd from the selected save's words.
   Never borrow the live window for a thumbnail: previewing a different slot
   must not change the running scene's private surfaces or counters. */
int bootstrap_saved_param_image(KBootstrap *b,const KFlags *saved,KImage *out){
    if(!b||!saved||!out||!saved->words||saved->word_count<=557)return -1;
    KBootstrap *state=calloc(1,sizeof(*state));if(!state)return -1;
    state->images=b->images;state->vm=b->vm;
    int failed=0;
    for(unsigned i=0;i<4;i++){
        int value=(int16_t)saved->words[500+i],limit=i==3?80:999;
        if(value<0){failed=1;break;}
        state->param_values[i]=(int16_t)(value>limit?limit:value);
    }
    state->param_markers[0]=(int16_t)saved->words[552];state->param_markers[1]=(int16_t)saved->words[553];
    state->param_total=saved->words[556];state->param_remaining=saved->words[557];
    if((int16_t)state->param_remaining>99)state->param_total=255;
    if(!failed)failed=param_window_rows(state,(int16_t)saved->words[541],1);
    if(!failed){
        KImage next=state->param_surface;state->param_surface=(KImage){0};
        next.height=state->param_rows==4?112:30+29*(unsigned)(state->param_rows-1);
        rmt_free(out);*out=next;
    }
    rmt_free(&state->param_surface);rmt_free(&state->param_atlas);free(state);return failed?-1:0;
}

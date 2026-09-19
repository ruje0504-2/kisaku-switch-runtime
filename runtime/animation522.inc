/* CFuncExec 31/522 (4fcef0): the four process-wide Sungeki windows.
 *
 * 0040c5d0 returns window objects whose private CDIBs are separate from the
 * script layers.  The portable runtime therefore keeps four 108x124 BGRA
 * images and composes them over layer 0 at the positions maintained by the
 * native +34 method.  The atlas writes and the keyed/alpha copy flags below
 * follow 0046cad0, 0046d350, 0046c8f0, 0046c760 and 0046cee0.  Native waits
 * 20 ms while moving a window; the completed position is deterministic, so
 * the VM observes the same final state without blocking the host thread.
 */

static int animation522_require(KBootstrap *b){
    /* 31/522 reads the 640x400 timepart/sungeki atlas.  The largest fixed
       source rectangle is (0x0,0x178,0x5c,0x18), while mode 0 writes the
       duplicated tile at x=0x208. */
    KImage *atlas=surface(b,7);
    if(!atlas||!atlas->pixels||atlas->width<628||atlas->height<400)
        return error(b,"31/522 layer 7 atlas missing or too small");
    for(unsigned i=0;i<4;i++){
        if(!b->exec522_sprites[i].pixels){
            uint8_t *p=calloc(108u*124u,4),*q=calloc(108u*124u,4);
            if(!p||!q){free(p);free(q);return error(b,"31/522 sprite allocation failed");}
            b->exec522_sprites[i]=(KImage){0,0,108,124,108u*4u,p};
            b->exec522_backing[i]=(KImage){0,0,108,124,108u*4u,q};
        }
    }
    return 0;
}

/* Copy an atlas rectangle into a window DIB.  CDIB+60 uses the low 24-bit
 * 0xff00 key; its final flag controls whether the destination Alpha byte is
 * copied.  The fixed native rectangles are validated before this helper is
 * called, so clipping would hide a malformed script and is intentionally not
 * used here. */
static int animation522_sprite_copy(KBootstrap *b,unsigned sprite,
                                    int dx,int dy,int width,int height,
                                    int sx,int sy,int key,int copy_alpha){
    KImage *src=surface(b,7),*dst=sprite<4?&b->exec522_sprites[sprite]:NULL;
    if(!src||!src->pixels||!dst||!dst->pixels)return error(b,"31/522 sprite surface missing");
    if(dx<0||dy<0||width<=0||height<=0||(uint64_t)dx+(unsigned)width>dst->width||
       (uint64_t)dy+(unsigned)height>dst->height||sx<0||sy<0||
       (uint64_t)sx+(unsigned)width>src->width||(uint64_t)sy+(unsigned)height>src->height)
        return error(b,"31/522 sprite copy outside surface");
    size_t bytes=(size_t)width*(size_t)height*4u;uint8_t *tmp=malloc(bytes);
    if(!tmp)return error(b,"31/522 sprite copy allocation failed");
    for(int y=0;y<height;y++)memcpy(tmp+(size_t)y*width*4,
        src->pixels+(size_t)(sy+y)*src->stride+(size_t)sx*4,(size_t)width*4);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        uint8_t *s=tmp+((size_t)y*width+(unsigned)x)*4;
        uint8_t *d=dst->pixels+(size_t)(dy+y)*dst->stride+(size_t)(dx+x)*4;
        if(key>=0&&((unsigned)s[0]|((unsigned)s[1]<<8)|((unsigned)s[2]<<16))==((unsigned)key&0xffffffu))continue;
        if(copy_alpha)memcpy(d,s,4);else memcpy(d,s,3);
    }
    free(tmp);return 0;
}

static int animation522_copy_atlas(KBootstrap *b,int dx,int dy,int width,int height,
                                   int sx,int sy,int copy_alpha){
    int32_t q[9]={dx,dy,width,height,7,sx,sy,7,copy_alpha};
    return blit_args(b,0,q);
}

static void animation522_restore(KBootstrap *b){
    /* CImage windows are composed by the native window manager after the
       scene/choice private surface is selected; the visible host target is
       always the real page-0 layer. */
    KImage *screen=&b->layers[0];
    if(!screen||!screen->pixels)return;
    for(unsigned i=0;i<4;i++)if(b->exec522_drawn[i]){
        int x=b->exec522_drawn_x[i],y=b->exec522_drawn_y[i];
        if(x>=0&&y>=0&&(unsigned)x+108<=screen->width&&(unsigned)y+124<=screen->height)
            for(unsigned row=0;row<124;row++)memcpy(screen->pixels+(size_t)(y+(int)row)*screen->stride+(size_t)x*4,
                b->exec522_backing[i].pixels+(size_t)row*b->exec522_backing[i].stride,108*4);
        b->exec522_drawn[i]=0;
    }
}

/* Called at the beginning of a host frame, before the normal scene code can
 * write through layer 0. */
static void animation522_begin_frame(KBootstrap *b){animation522_restore(b);}

static void animation522_present(KBootstrap *b){
    KImage *screen=&b->layers[0];
    if(!screen||!screen->pixels)return;
    for(unsigned i=0;i<4;i++)if(b->exec522_visible[i]&&b->overlays.visible[i]){
        int x=b->exec522_x[i],y=b->exec522_y[i];
        if(x<0||y<0||(unsigned)x+108>screen->width||(unsigned)y+124>screen->height){error(b,"31/522 sprite position outside screen");return;}
        for(unsigned row=0;row<124;row++)memcpy(b->exec522_backing[i].pixels+(size_t)row*b->exec522_backing[i].stride,
            screen->pixels+(size_t)(y+(int)row)*screen->stride+(size_t)x*4,108*4);
        for(unsigned row=0;row<124;row++)for(unsigned col=0;col<108;col++){
            uint8_t *s=b->exec522_sprites[i].pixels+(size_t)row*b->exec522_sprites[i].stride+(size_t)col*4;
            if(!s[3])continue;
            uint8_t *d=screen->pixels+(size_t)(y+(int)row)*screen->stride+(size_t)(x+(int)col)*4;
            unsigned a=s[3],inv=255u-a;
            for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(((unsigned)s[c]*a+(unsigned)d[c]*inv)/255u);
            d[3]=(uint8_t)(a+(unsigned)d[3]*inv/255u);
        }
        b->exec522_drawn[i]=1;b->exec522_drawn_x[i]=x;b->exec522_drawn_y[i]=y;
    }
}

static int animation522_mode0(KBootstrap *b,int selector){
    /* 004fcef0 consumes one selector before 0046cad0.  The selector is
       handled by the preceding native 00428f00 lookup; the six fixed blits
       below do not branch on its returned field.  Keep it in the portable
       call boundary even though the draw sequence is invariant. */
    (void)selector;
    if(animation522_require(b))return -1;
    animation522_restore(b);
    /* 0046cad0 first seeds every native sprite with the keyed tile. */
    for(unsigned i=0;i<4;i++)if(animation522_sprite_copy(b,i,0,0,0x6c,0x7c,0x198,0xd4,0xff00,1))return -1;
    /* The native CDIB self-copies prepare two atlas rows for later modes. */
    if(animation522_copy_atlas(b,0x208,0,0x6c,0x7c,0x198,0xd4,1))return -1;
    if(animation522_copy_atlas(b,0x208,0x7c,0x6c,0x7c,0x198,0xd4,1))return -1;
    for(unsigned i=0;i<4;i++)if(animation522_sprite_copy(b,i,0xc,0x18,0x54,0x20,0x5c,0xa0+i*0x20,-1,1))return -1;
    b->exec522_current=-1;
    animation522_present(b);return b->error[0]?-1:0;
}

static int animation522_mode1(KBootstrap *b,int target){
    if(target<0||target>3)return error(b,"31/522 mode 1 target out of range (arguments preserved)");
    if(animation522_require(b))return -1;
    /* 0046d350 copies the common frame through the prepared atlas row. */
    if(animation522_copy_atlas(b,0x208,0x3c,0x5c,0x30,0,0xe8,1))return -1;
    if(animation522_sprite_copy(b,(unsigned)target,8,0x3c,0x5c,0x30,0x208,0x7c,-1,1))return -1;
    for(unsigned i=0;i<4;i++){b->exec522_visible[i]=0;b->overlays.visible[i]=0;}
    b->exec522_visible[target]=1;b->overlays.visible[target]=1;
    /* Initial entry ends at x=0x10+6*0x1a; subsequent entries move by ten
       12-pixel steps, so the same closed form is 0xac+target*0x78. */
    b->exec522_x[target]=0xac+target*0x78;b->exec522_y[target]=0x10;
    b->exec522_current=target;b->exec522_state=1;
    animation522_restore(b);animation522_present(b);return b->error[0]?-1:0;
}

static int animation522_mode3(KBootstrap *b,int item){
    if(item<1||item>3)return 0; /* 0046c8f0 deliberately ignores zero/4+. */
    if(animation522_require(b))return -1;
    if(b->exec522_current<0||b->exec522_current>3)
        return error(b,"31/522 mode 3 has no current sprite (arguments preserved)");
    int sy=0xd0-(item-1)*0x18;
    if(animation522_copy_atlas(b,0x210,0x54,0x5c,0x18,0,0x118,1))return -1;
    if(animation522_sprite_copy(b,(unsigned)b->exec522_current,8,0x54,0x5c,0x18,0,sy,-1,1))return -1;
    b->exec522_state=0;animation522_restore(b);animation522_present(b);return b->error[0]?-1:0;
}

static int animation522_mode4(KBootstrap *b,int item,int group){
    if(!item)return 0; /* Native 0046c760 returns without drawing. */
    if(item<1||item>2||group<0||group>2)return error(b,"31/522 mode 4 selector out of range (arguments preserved)");
    if(animation522_require(b))return -1;
    if(b->exec522_current<0||b->exec522_current>3)
        return error(b,"31/522 mode 4 has no current sprite (arguments preserved)");
    static const int second_y[]={0x178,0x148,0x130};
    int sy=item==1?0x160:second_y[group];
    if(animation522_sprite_copy(b,(unsigned)b->exec522_current,8,0x54,0x5c,0x18,0,sy,0xff00,0))return -1;
    b->exec522_state=0;animation522_restore(b);animation522_present(b);return b->error[0]?-1:0;
}

static int animation522_mode5(KBootstrap *b,int packed,int variant){
    /* Case 5 consumes a packed window id and a variant before the native
       cleanup.  0046cee0 itself only moves/destroys the four windows; retain
       both values at the boundary while applying that cleanup. */
    (void)packed;(void)variant;
    /* 0046cee0 moves and destroys all four native windows.  Restore the
       covered page immediately, then leave no visible windows behind. */
    animation522_restore(b);
    for(unsigned i=0;i<4;i++){b->exec522_visible[i]=0;b->overlays.visible[i]=0;}
    b->exec522_current=-1;b->exec522_state=0;return 0;
}

static int animation522_draw(KBootstrap *b,int mode,const int *args,unsigned count){
    switch(mode){
    case 0: if(count!=1)return error(b,"31/522 mode 0 selector missing (arguments preserved)");return animation522_mode0(b,args[0]);
    case 1: if(count!=1)return error(b,"31/522 mode 1 target missing (arguments preserved)");return animation522_mode1(b,args[0]);
    case 2: if(count!=1)return error(b,"31/522 mode 2 selector missing (arguments preserved)");b->exec522_state=2;return 0;
    case 3: if(count!=1)return error(b,"31/522 mode 3 item missing (arguments preserved)");return animation522_mode3(b,args[0]);
    case 4: if(count!=2)return error(b,"31/522 mode 4 selectors missing (arguments preserved)");return animation522_mode4(b,args[0],args[1]);
    case 5: if(count!=2)return error(b,"31/522 mode 5 selectors missing (arguments preserved)");return animation522_mode5(b,args[0],args[1]);
    default:return error(b,"31/522 mode unsupported (arguments preserved)");
    }
}

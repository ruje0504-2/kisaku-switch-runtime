/* CFuncExec 31/523: 4fce50 -> 508b70/508620. Each native row is
 * presented before a fresh 508540(20) wait, including the last row. */
static int animation523_surface(KBootstrap *b,int id,unsigned width,unsigned height,
                                const char *name){
    KImage *im=surface(b,id);
    if(!im||!im->pixels||im->width<width||im->height<height)
        return error(b,name);
    return 0;
}

/* CDIB::virtual_48: dx,dy,w,h,source,sx,sy,copy-alpha. */
static int animation523_copy(KBootstrap *b,int dst,int dx,int dy,int width,int height,
                             int src,int sx,int sy,int copy_alpha){
    int32_t q[9]={dx,dy,width,height,dst,sx,sy,src,copy_alpha};
    return blit_args(b,0,q);
}

/* CDIB::virtual_60 in these two functions is called with color key 0xff00
 * and copy-alpha 0.  blit_args(keyed=1) therefore copies RGB and leaves the
 * destination Alpha byte untouched, matching the native method. */
static int animation523_key(KBootstrap *b,int dst,int dx,int dy,int width,int height,
                            int src,int sx,int sy){
    int32_t q[9]={dx,dy,width,height,dst,sx,sy,src,0xff00};
    return blit_args(b,1,q);
}

static int animation523_require(KBootstrap *b,int mode){
    /* Maximum coordinates reached by the fixed native tables.  The source
     * layer 3 is read back from y=0xc4 after being built at y=0xc4. */
    if(animation523_surface(b,0,608,288,"31/523 display surface missing"))return -1;
    if(animation523_surface(b,2,560,180,"31/523 layer 2 surface missing"))return -1;
    if(animation523_surface(b,3,576,392,"31/523 layer 3 surface missing"))return -1;
    if(animation523_surface(b,7,216,326,"31/523 layer 7 atlas missing"))return -1;
    if(mode==0&&animation523_surface(b,1,608,288,"31/523 layer 1 source missing"))return -1;
    return 0;
}

static int animation523_mode0(KBootstrap *b,int row){
    /* Draw the lower board from the preceding iteration. */
    if(animation523_copy(b,3,0,0xc4,0x240,0xc4,3,0,0,0))return -1;
    if(animation523_key(b,3,0,0xc4,8,8,7,0xc0,0x130))return -1;
    int x=8;
    for(int i=0;i<row*0xe;i++,x+=8)
        if(animation523_key(b,3,x,0xc4,8,8,7,200,0x130))return -1;
    if(animation523_key(b,3,x,0xc4,8,8,7,0xd0,0x130))return -1;
    int tile=0;
    for(int i=0;i<row*6;i++,tile++){
        int y=i*6+0xcc;
        if(animation523_key(b,3,0,y,8,6,7,0xc0,0x138))return -1;
        if(animation523_copy(b,3,8,y,row*0x70,6,2,0,i*6,1))return -1;
        if(animation523_key(b,3,row*0x70+8,y,8,6,7,0xd0,0x138))return -1;
    }
    int y=tile*6+0xcc;
    if(animation523_key(b,3,0,y,8,8,7,0xc0,0x13e))return -1;
    x=8;
    for(int i=0;i<row*0xe;i++,x+=8)
        if(animation523_key(b,3,x,y,8,8,7,200,0x13e))return -1;
    if(animation523_key(b,3,x,y,8,8,7,0xd0,0x13e))return -1;
    /* 00508b70 presents layer 3's lower 576x196 window on layer 0. */
    if(animation523_copy(b,0,0x20,0x5c,0x240,0xc4,3,0,0xc4,0))return -1;
    return 0;
}

static int animation523_mode1(KBootstrap *b,int row){
    if(animation523_copy(b,3,0,0xc4,0x240,0xc4,3,0,0,1))return -1;
    if(animation523_copy(b,2,0,0,0x230,0xb4,0,0x28,100,1))return -1;
    int x=0x230-row*0x70;
    int y=0x178-row*0x24;
    if(animation523_key(b,3,x,y,8,8,7,0xc0,0x130))return -1;
    x+=8;
    for(int i=0;i<row*0xe;i++,x+=8)
        if(animation523_key(b,3,x,y,8,8,7,200,0x130))return -1;
    if(animation523_key(b,3,x,y,8,8,7,0xd0,0x130))return -1;
    int tile=0;
    y=0x180-row*0x24;
    for(int i=0;i<row*6;i++,tile++){
        if(animation523_key(b,3,0x230-row*0x70,y,8,6,7,0xc0,0x138))return -1;
        if(animation523_copy(b,3,0x238-row*0x70,y,row*0x70,6,2,
                             0x230-row*0x70,(row*6-tile)*-6+0xb4,1))return -1;
        if(animation523_key(b,3,0x238-row*0x70+row*0x70,y,8,6,7,0xd0,0x138))return -1;
        y+=6;
    }
    if(animation523_key(b,3,0x230-row*0x70,y,8,8,7,0xc0,0x13e))return -1;
    x=0x238-row*0x70;
    for(int i=0;i<row*0xe;i++,x+=8)
        if(animation523_key(b,3,x,y,8,8,7,200,0x13e))return -1;
    if(animation523_key(b,3,x,y,8,8,7,0xd0,0x13e))return -1;
    if(animation523_copy(b,0,0x20,0x5c,0x240,0xc4,3,0,0xc4,0))return -1;
    return 0;
}

static int animation523_draw(KBootstrap *b,int mode){
    if(mode!=0&&mode!=1)return error(b,"31/523 mode unsupported (arguments preserved)");
    if(b->exec523_active)return error(b,"31/523 animation already active (arguments preserved)");
    if(animation523_require(b,mode))return -1;
    if(!mode&&animation523_copy(b,3,0,0,0x240,0xc4,1,0x20,0x5c,0))return -1;
    if(mode?animation523_mode1(b,5):animation523_mode0(b,0))return -1;
    b->exec523_active=1;b->exec523_mode=(unsigned)mode;
    b->exec523_step=0;b->exec523_clock=0;
    return 0;
}

static int animation523_frame(KBootstrap *b){
    if(!b->exec523_active)return 0;
    /* 508540 / 4e25f0: Shift/Ctrl or sys50 flag 8000 accelerates by 16.
       byte4012==1 suppresses only script acceleration, not the keys. */
    int fast=b->effect_fast||((b->vm->globals[0][50].number&0x8000)&&
        !(b->vm->byte_count>4012&&b->vm->bytes[4012]==1));
    b->exec523_clock+=fast?16000:1000;
    if(b->exec523_clock<1200)return 0;
    /* 00508540 accumulates elapsed wall-clock time from one wait to the next.
       Keep the fractional remainder when a 60 Hz host crosses the 20 ms
       deadline; dropping it turns every wait into a fixed two-frame (33 ms)
       dwell and stretches this 120 ms native sequence to 200 ms.  The
       carried remainder produces the expected 2,1,1,1,1,2 frame cadence
       around the 20 ms deadlines without allowing a single frame to skip
       rows. The host remains a 60 Hz display and therefore cannot present
       the PC's millisecond boundaries exactly. */
    b->exec523_clock-=1200;
    if(++b->exec523_step==6){
        if(b->exec523_mode&&animation523_copy(b,0,0x20,0x5c,0x240,0xc4,3,0,0,0))return -1;
        b->exec523_active=0;return 0;
    }
    return b->exec523_mode?animation523_mode1(b,5-(int)b->exec523_step):
        animation523_mode0(b,(int)b->exec523_step);
}

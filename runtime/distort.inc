/* 42c180: horizontal double image, brief white flash, then original frame.
 * Arguments are displacement, reserved (ignored natively), repetition count.
 * Native routine uses page 2 as its snapshot and page 3 as a white plane.
 */
static void distort_blend(KBootstrap *b,int dx,int sx,int width,unsigned alpha){
    KImage *dst=&b->layers[0],*src=&b->layers[2];
    if(dx<0){sx-=dx;width+=dx;dx=0;}if(sx<0){dx-=sx;width+=sx;sx=0;}
    if(width>640-dx)width=640-dx;
    if(width>640-sx)width=640-sx;
    if(width<=0)return;
    for(int y=0;y<480;y++)for(int x=0;x<width;x++){
        uint8_t *d=dst->pixels+y*dst->stride+(dx+x)*4,*s=src->pixels+y*src->stride+(sx+x)*4;
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(s[c]*alpha/255+d[c]*(255-alpha)/255);
    }
}
static void distort_frame(KBootstrap *b){
    if(!b->distort_count)return;
    KDistortFrame *f=&b->distort[b->distort_index++];
    for(unsigned y=0;y<480;y++)memcpy(b->layers[0].pixels+y*b->layers[0].stride,b->layers[2].pixels+y*b->layers[2].stride,640*4);
    if(f->primary){distort_blend(b,f->shift,0,640-f->shift,f->primary);distort_blend(b,0,f->shift,640-f->shift,f->secondary);}
    if(f->white)for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *d=b->layers[0].pixels+y*b->layers[0].stride+x*4;
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(f->white+d[c]*(255-f->white)/255);
    }
    if(b->distort_index==b->distort_count)b->distort_count=b->distort_index=0;
}
static int distort_begin(KBootstrap *b){
    int32_t displacement,reserved,count;
    if(integer(b,&displacement)||integer(b,&reserved)||integer(b,&count))return -1;
    (void)reserved;
    if(displacement<0||count<1||count>128||(int64_t)count*displacement>=640)return error(b,"distortion range invalid");
    for(unsigned i=0;i<4;i++)if(i!=1&&(!b->layers[i].pixels||b->layers[i].width<640||b->layers[i].height<480))return error(b,"distortion surface missing");
    for(unsigned y=0;y<480;y++){
        memcpy(b->layers[2].pixels+y*b->layers[2].stride,b->layers[0].pixels+y*b->layers[0].stride,640*4);
        for(unsigned x=0;x<640;x++)memset(b->layers[3].pixels+y*b->layers[3].stride+x*4,255,3);
    }
    unsigned n=0;int shift=0,level=256,step=1,decay=256/count;
    for(int i=0;i<count;i++){shift+=displacement;b->distort[n++]=(KDistortFrame){shift,200,100,0};}
    for(int i=0;i<count;i++){
        shift-=step;level-=decay;if(shift<0)break;
        b->distort[n++]=(KDistortFrame){shift,200,100,(uint8_t)(255-(uint16_t)level)};step*=2;
    }
    for(unsigned i=0;i<2;i++){shift+=step;level-=decay;b->distort[n++]=(KDistortFrame){shift,128,64,(uint8_t)(255-(uint16_t)level)};}
    for(unsigned alpha=0;alpha<127;alpha+=8)b->distort[n++]=(KDistortFrame){0,alpha,0,0};
    b->distort[n++]=(KDistortFrame){0,0,0,0};b->distort_count=n;b->distort_index=0;
    return 0;
}

/* 42c890: close black horizontal bands from both edges, hold 200 ms, reopen. */
static int blink_begin(KBootstrap *b){
    if(b->fade_visible||b->fade_steps||b->helper_steps)return error(b,"blink overlay already in use");
    if(!b->canvas.pixels||b->canvas.width!=640||b->canvas.height!=480)return error(b,"blink canvas missing");
    if(!b->fade_surface.pixels){b->fade_surface=(KImage){0,0,640,480,2560,calloc(640*480,4)};if(!b->fade_surface.pixels)return error(b,"blink allocation failed");}
    memset(b->canvas.pixels,0,640*480*4);memset(b->fade_surface.pixels,0,640*480*4);
    b->blink_active=1;b->blink_step=b->blink_clock=0;b->blink_wait=900;b->fade_visible=1;b->fade_alpha=255;return 0;
}
static void blink_frame(KBootstrap *b){
    if(!b->blink_active)return;
    b->blink_clock+=1000;
    while(b->blink_active&&b->blink_clock>=b->blink_wait){
        b->blink_clock-=b->blink_wait;
        unsigned step=b->blink_step++,closing=step<10;
        unsigned y=closing?step*24:(19-step)*24;
        for(unsigned row=0;row<24;row++)for(unsigned x=0;x<640;x++){
            b->fade_surface.pixels[(y+row)*2560+x*4+3]=closing?255:0;
            b->fade_surface.pixels[(456-y+row)*2560+x*4+3]=closing?255:0;
        }
        /* 42c890 holds the closed overlay for 200 ms. Re-arm the clock with the
           new wait: carrying the remainder of the previous 900-unit step left
           only eleven fully black frames (183 ms at 60 Hz), not twelve. */
        b->blink_wait=b->blink_step==10?12000:900;
        b->blink_clock=0;
        if(b->blink_step==20){b->blink_active=0;b->fade_visible=0;}
    }
}

/* 42bd20: vertical scroll of page 2; direction 0 joins two 480-row panels. */
static void scroll_frame(KBootstrap *b){
    unsigned pos=b->scroll_offset,target=b->scroll_target;
    if(pos>target)pos=target;
    KImage *dst=&b->layers[0],*src=&b->layers[2];
    for(unsigned y=0;y<480;y++){
        unsigned sy=pos+y;
        if(b->scroll_wrap&&y>=480-pos)sy=480+y-(480-pos);
        memcpy(dst->pixels+y*dst->stride,src->pixels+sy*src->stride,640*4);
    }
    if(b->message_visible){memcpy(b->message_base.pixels,dst->pixels,640*480*4);message_compose(b);}
    if(pos==target)b->scroll_active=0;else b->scroll_offset+=b->scroll_speed;
}
static int scroll_begin(KBootstrap *b){
    int32_t speed,reserved,target,direction;
    if(integer(b,&speed)||integer(b,&reserved)||integer(b,&target)||integer(b,&direction))return -1;
    (void)speed;(void)reserved;
    if(!direction)target=480;
    if(target<0||target>8192||!b->layers[0].pixels||b->layers[0].width!=640||b->layers[0].height!=480||!b->layers[2].pixels||b->layers[2].width<640||(uint64_t)target+480>b->layers[2].height)return error(b,"scroll background range invalid");
    int effect=option(b,"Display","EffectSpeed",0);
    b->scroll_speed=effect==1?8:effect==2?16:2;
    b->scroll_offset=0;b->scroll_target=(unsigned)target;b->scroll_wrap=!direction;b->scroll_active=1;
    if(!target)scroll_frame(b);
    return 0;
}

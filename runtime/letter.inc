/* Kisaku CLetter +ec/+f0/+f4. Kept separate from inherited novel surfaces. */
static int letter_finish(KBootstrap *b){
    b->letter_transition=0;
    if(message_init(b))return -1;
    b->novel_mode=0;return 0;
}
static int letter_render(KBootstrap *b){
    KImage *screen=&b->layers[0],*from=&b->letter_surfaces[0],*to=&b->letter_surfaces[1];
    unsigned alpha=b->letter_alpha;
    for(unsigned y=0;y<480;y++){
        uint8_t *out=screen->pixels+y*screen->stride;
        const uint8_t *src=from->pixels+y*from->stride,*dst=to->pixels+y*to->stride;
        if(alpha>=255){memcpy(out,dst,640*4);continue;}
        for(unsigned x=0;x<640;x++){
            for(unsigned c=0;c<3;c++)out[x*4+c]=(uint8_t)(src[x*4+c]*(255-alpha)/255+dst[x*4+c]*alpha/255);
            out[x*4+3]=src[x*4+3];
        }
    }
    return alpha>=255?letter_finish(b):0;
}
static int letter_exit(KBootstrap *b){
    KImage *screen=&b->layers[0];
    if(b->letter_transition||!screen->pixels||screen->width<640||screen->height<480)
        return error(b,"CLetter exit requires idle 640x480 screen");
    for(unsigned i=0;i<3;i++)if(!b->letter_surfaces[i].pixels){
        uint8_t *p=calloc(640*480,4);if(!p)return error(b,"CLetter surface allocation failed");
        b->letter_surfaces[i]=(KImage){0,0,640,480,640*4,p};
    }
    /* 48adf0 copies the saved background to the destination before replacing
       the saved background with the current screen. */
    memcpy(b->letter_surfaces[1].pixels,b->letter_surfaces[0].pixels,640*480*4);
    for(unsigned y=0;y<480;y++)memcpy(b->letter_surfaces[0].pixels+y*2560,screen->pixels+y*screen->stride,2560);
    b->font_width=b->font_height=16;
    int speed=option(b,"Display","EffectSpeed",0);
    b->letter_increment=speed==1?64:speed==2?256:32;
    b->letter_alpha=b->letter_increment;b->letter_transition=1;
    return letter_render(b);
}
static void letter_frame(KBootstrap *b){
    b->letter_alpha+=b->letter_increment;
    (void)letter_render(b);
}

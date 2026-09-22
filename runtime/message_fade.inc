/* 31/43 -> 4f95a0 -> CMesFadeSprite 485f40. Private sprites have
   priorities 3 (shade), 4 (old text), 5 (incoming text); no VM layers owned. */
static void message_fade_restore(KBootstrap *b){
    if(!b->mes_fade_drawn)return;
    KImage *screen=&b->layers[0];
    for(unsigned y=0;y<480;y++)memcpy(screen->pixels+y*screen->stride,
        b->mes_fade_backing.pixels+y*2560,2560);
    b->mes_fade_drawn=0;
}
/* 46ab10 -> CDIB+5c -> 48c200: scale source alpha, then separately
   truncate each color contribution. The destination alpha is unchanged. */
static void message_fade_blend(KImage *screen,const KImage *sprite,unsigned opacity){
    if(!opacity)return;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *s=sprite->pixels+y*2560+x*4;
        uint8_t *d=screen->pixels+y*screen->stride+x*4;
        unsigned a=s[3]*opacity/255;
        if(a)for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(d[c]*(255-a)/255+s[c]*a/255);
    }
}
/* 4b9850 -> 46b1e0(mode=3, show=1, hard=0) -> 46a650.
   The native mask changes pixel alpha by a fixed 512/steps increment,
   bounded by the saved source alpha; it is not a uniform crossfade. */
static void message_fade_mask_blend(KBootstrap *b,KImage *screen){
    unsigned threshold=b->mes_fade_steps?(512u/b->mes_fade_steps)*b->mes_fade_tick:512;
    const KImage *sprite=&b->mes_fade_surfaces[0];
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *s=sprite->pixels+y*2560+x*4;
        unsigned mask=b->mes_fade_mask[y*640+x];
        unsigned a=threshold>mask?threshold-mask:0;
        if(a>s[3])a=s[3];
        if(a){
            uint8_t *d=screen->pixels+y*screen->stride+x*4;
            for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(d[c]*(255-a)/255+s[c]*a/255);
        }
    }
}
static void message_fade_present(KBootstrap *b){
    if(b->mes_fade_drawn||!b->mes_fade_backing.pixels||
       (!b->mes_fade_visible&&!b->mes_fade_shade_visible&&b->mes_fade_transition!=3))return;
    KImage *screen=&b->layers[0];
    for(unsigned y=0;y<480;y++)memcpy(b->mes_fade_backing.pixels+y*2560,
        screen->pixels+y*screen->stride,2560);
    if(b->mes_fade_shade_visible)message_fade_blend(screen,&b->mes_fade_surfaces[2],b->mes_fade_shade_alpha);
    if(b->mes_fade_visible)message_fade_blend(screen,&b->mes_fade_surfaces[1],b->mes_fade_alpha);
    if(b->mes_fade_transition==3){
        if(b->mes_fade_type==3)message_fade_mask_blend(b,screen);
        else {
            unsigned alpha=b->mes_fade_type==1?255-b->mes_fade_alpha:
                (unsigned)((uint64_t)255*b->mes_fade_tick/b->mes_fade_steps);
            message_fade_blend(screen,&b->mes_fade_surfaces[0],alpha);
        }
    }
    if(b->present_mes_valid){
        if(present_surface(&b->present_mes_reference,640,480))b->present_mes_valid=0;
        else for(unsigned y=0;y<480;y++)memcpy(b->present_mes_reference.pixels+y*2560,screen->pixels+y*screen->stride,2560);
    }
    b->mes_fade_drawn=1;
}
static void present_mes_layers(KBootstrap *b){
    memcpy(b->present_clean.pixels,b->mes_fade_backing.pixels,640*480*4);
    if(b->mes_fade_shade_visible)message_fade_blend(&b->present_clean,&b->mes_fade_surfaces[2],b->mes_fade_shade_alpha);
    /* Capture the native reference before later sprites cover this surface. */
    memcpy(b->present_reference.pixels,b->present_mes_reference.pixels,640*480*4);
    unsigned next=b->mes_fade_transition==3?(b->mes_fade_type==1?255-b->mes_fade_alpha:
        (unsigned)((uint64_t)255*b->mes_fade_tick/b->mes_fade_steps)):0;
    unsigned threshold=b->mes_fade_steps?(512u/b->mes_fade_steps)*b->mes_fade_tick:512;
    for(unsigned y=0;y<720;y++)for(unsigned x=0;x<960;x++){
        size_t i=((size_t)y*960+x)*4;
        const uint8_t *a=b->present_mes_old.pixels+i,*z=b->present_mes_new.pixels+i;
        uint8_t *d=b->present_overlay.pixels+i;
        unsigned aa=b->mes_fade_visible?a[3]*b->mes_fade_alpha/255:0,za=z[3]*next/255;
        if(b->mes_fade_transition==3&&b->mes_fade_type==3){
            unsigned mask=b->mes_fade_mask[(y*2/3)*640+x*2/3];
            za=threshold>mask?threshold-mask:0;if(za>z[3])za=z[3];
        }
        unsigned old_weight=aa*(255-za)/255,out=za+old_weight;
        if(!out)continue;
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)((z[c]*za+a[c]*old_weight)/out);
        d[3]=(uint8_t)out;
    }
}
static void message_fade_finish(KBootstrap *b){
    if(b->present_mes_valid&&b->mes_fade_transition==3){
        if(b->mes_fade_type==1)memcpy(b->present_mes_old.pixels,b->present_mes_new.pixels,960*720*4);
        else{
            const unsigned *r=b->mes_fade_rect;
            unsigned x=(r[0]*3+1)/2,end=((r[0]+r[2])*3+1)/2;
            for(unsigned y=(r[1]*3+1)/2;y<((r[1]+r[3])*3+1)/2;y++)
                memcpy(b->present_mes_old.pixels+y*3840+x*4,b->present_mes_new.pixels+y*3840+x*4,(end-x)*4);
        }
        memset(b->present_mes_new.pixels,0,960*720*4);
    }
    switch(b->mes_fade_transition){
    case 3:
        if(b->mes_fade_type==1){
            memcpy(b->mes_fade_surfaces[1].pixels,b->mes_fade_surfaces[0].pixels,480*2560);
            b->mes_fade_alpha=255;
        }else{
            /* 485c49/485cd1 commit only this line, preserving earlier lines. */
            const unsigned *r=b->mes_fade_rect;
            for(unsigned y=r[1];y<r[1]+r[3];y++)
                memcpy(b->mes_fade_surfaces[1].pixels+y*2560+r[0]*4,
                       b->mes_fade_surfaces[0].pixels+y*2560+r[0]*4,r[2]*4);
        }
        memset(b->mes_fade_surfaces[0].pixels,0,480*2560);break;
    case 4:b->mes_fade_visible=0;b->mes_fade_alpha=0;break;
    case 5:b->mes_fade_shade_alpha=255;break;
    case 6:b->mes_fade_shade_visible=0;b->mes_fade_shade_alpha=0;break;
    }
    b->mes_fade_transition=0;
}
static void message_fade_frame(KBootstrap *b){
    /* 46b5d0 advances one interpolation step per 15ms (46bca0).
       Initial tick zero is rendered when the native operation starts. */
    b->mes_fade_clock+=1000;
    while(b->mes_fade_clock>=900&&b->mes_fade_transition){
        b->mes_fade_clock-=900;
        if(++b->mes_fade_tick>=b->mes_fade_steps){message_fade_finish(b);break;}
        unsigned progress=(unsigned)((uint64_t)255*b->mes_fade_tick/b->mes_fade_steps);
        if((b->mes_fade_transition==3&&b->mes_fade_type==1)||b->mes_fade_transition==4)b->mes_fade_alpha=255-progress;
        else if(b->mes_fade_transition!=3) b->mes_fade_shade_alpha=b->mes_fade_transition==5?progress:255-progress;
    }
}
static void message_fade_release(KBootstrap *b){
    b->present_mes_valid=0;
    rmt_free(&b->present_mes_reference);rmt_free(&b->present_mes_source);rmt_free(&b->present_mes_shadow);
    rmt_free(&b->present_mes_old);rmt_free(&b->present_mes_new);
    for(unsigned i=0;i<3;i++)rmt_free(&b->mes_fade_surfaces[i]);
    free(b->mes_fade_mask);b->mes_fade_mask=NULL;b->mes_fade_type=0;
    b->mes_fade_visible=b->mes_fade_shade_visible=b->mes_fade_transition=0;
    b->mes_fade_alpha=b->mes_fade_shade_alpha=255;
}
static int message_fade_call(KBootstrap *b,int mode){
    if(b->mes_fade_transition)return error(b,"CMesFadeSprite transition busy (arguments preserved)");
    if(mode==1){message_fade_release(b);return 0;} /* 4852a0 */
    if(mode==0){ /* 485e20: private full-screen bitmaps, hidden initially. */
        KImage images[3]={{0}};
        for(unsigned i=0;i<3;i++){
            images[i]=(KImage){0,0,640,480,2560,calloc(480,2560)};
            if(!images[i].pixels){
                for(unsigned j=0;j<3;j++)rmt_free(&images[j]);
                return error(b,"CMesFadeSprite allocation failed (arguments preserved)");
            }
        }
        for(unsigned i=0;i<640*480;i++)images[2].pixels[i*4+3]=100;
        message_fade_release(b);
        memcpy(b->mes_fade_surfaces,images,sizeof(images));return 0;
    }
    if(mode==2){ /* 4853e0: clear, show transparent text sprite, reset clip. */
        KImage *dst=surface(b,b->vm->globals[0][49].number);
        if(!b->mes_fade_surfaces[0].pixels||!b->mes_fade_surfaces[1].pixels)
            return error(b,"CMesFadeSprite reset before initialization (arguments preserved)");
        if(!dst||!dst->pixels)return error(b,"CMesFadeSprite target missing (arguments preserved)");
        for(unsigned i=0;i<2;i++)memset(b->mes_fade_surfaces[i].pixels,0,480*2560);
        unsigned width=dst->width<640?dst->width:640,height=dst->height<480?dst->height:480;
        for(unsigned y=0;y<height;y++)memset(dst->pixels+y*dst->stride,0,width*4);
        present_mes_reset(b,dst);
        b->mes_fade_visible=1;
        const int rect[4]={0,0,640,480};
        for(unsigned i=0;i<4;i++)b->vm->globals[0][42+i]=(KValue){rect[i],NULL};
        return 0;
    }
    if(mode>=3&&mode<=6){
        if(!b->mes_fade_surfaces[0].pixels||!b->layers[0].pixels||
           b->layers[0].width!=640||b->layers[0].height!=480)
            return error(b,"CMesFadeSprite requires initialized 640x480 screen (arguments preserved)");
        KVM *v=b->vm;int duration=v->stack[v->sp-3].number;
        if(duration<0)return error(b,"CMesFadeSprite negative duration (arguments preserved)");
        KImage *source=NULL;int x=0,y=0,w=0,h=0,type=0;
        if(mode==3){
            /* 4858c0 consumes duration, an unused numeric value, then type. */
            type=v->stack[v->sp-5].number;
            if(type<1||type>3)
                return error(b,"CMesFadeSprite effect type unsupported (arguments preserved)");
            source=surface(b,v->globals[0][49].number);
            int64_t width=(int64_t)v->globals[0][46].number+2;
            int64_t height=v->globals[0][31].number;
            int64_t left=(640-(int64_t)v->globals[0][46].number)/2;
            int64_t top=v->globals[0][47].number;
            if(!source||!source->pixels||width<=0||height<=0||left<0||top<0||
               left+width>640||top+height>480||width>source->width||top+height>source->height)
                return error(b,"CMesFadeSprite message rectangle invalid (arguments preserved)");
            x=(int)left;y=(int)top;w=(int)width;h=(int)height;
        }
        if(!b->mes_fade_backing.pixels){
            uint8_t *data=calloc(480,2560);
            if(!data)return error(b,"CMesFadeSprite backing allocation failed (arguments preserved)");
            b->mes_fade_backing=(KImage){0,0,640,480,2560,data};
        }
        if(type==3&&!b->mes_fade_mask){
            uint8_t *mask=NULL;size_t size=0;
            if(read_named(&b->data,"03.bin",&mask,&size)||size<640*480){
                free(mask);return error(b,"CMesFadeSprite mask 03.bin invalid (arguments preserved)");
            }
            b->mes_fade_mask=mask;
        }
        if((mode==5&&b->mes_fade_shade_visible)||(mode==6&&!b->mes_fade_shade_visible))return 0;
        if(mode==3){
            KImage *target=&b->mes_fade_surfaces[0];
            for(int row=0;row<h;row++)memcpy(target->pixels+(y+row)*2560+x*4,
                source->pixels+(y+row)*source->stride,(size_t)w*4);
            if(present_mes_matches(b,source)){
                unsigned left=((unsigned)x*3+1)/2,right=((unsigned)(x+w)*3+1)/2;
                for(unsigned row=((unsigned)y*3+1)/2;row<((unsigned)(y+h)*3+1)/2;row++)
                    memcpy(b->present_mes_new.pixels+row*3840+left*4,b->present_mes_source.pixels+row*3840,(right-left)*4);
            }else b->present_mes_valid=0;
            b->mes_fade_type=(unsigned)type;
            b->mes_fade_rect[0]=(unsigned)x;b->mes_fade_rect[1]=(unsigned)y;
            b->mes_fade_rect[2]=(unsigned)w;b->mes_fade_rect[3]=(unsigned)h;
            if(type==1)b->mes_fade_visible=1;
        }
        if(mode==5)b->mes_fade_shade_visible=1;
        /* 46bbe0: flag 0x4000 bypasses speed scaling, not the transition. */
        unsigned steps=(unsigned)duration;
        if(!(v->globals[0][50].number&0x4000)){
            int speed=option(b,"Display","EffectSpeed",0);
            if(speed==0)steps>>=1;else if(speed==1)steps>>=2;else if(speed==2)steps=0;
        }
        b->mes_fade_steps=steps;b->mes_fade_tick=b->mes_fade_clock=0;
        b->mes_fade_transition=(unsigned)mode;
        if((mode==3&&type==1)||mode==4)b->mes_fade_alpha=255;
        else if(mode!=3)b->mes_fade_shade_alpha=mode==5?0:255;
        if(!steps)message_fade_finish(b);
        return 0;
    }
    return error(b,"CMesFadeSprite unsupported mode (arguments preserved)");
}

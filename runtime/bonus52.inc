/* Native 438f30: mouse movement fills a meter; elapsed time selects the
   script result. Switch also accepts repeated A presses or stick motion. */
static void bonus52_blend(KImage *dst,const KImage *src,unsigned dx,unsigned dy){
    for(unsigned y=0;y<src->height;y++)for(unsigned x=0;x<src->width;x++){
        const uint8_t *p=src->pixels+y*src->stride+x*4;uint8_t *q=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
        for(unsigned c=0;c<3;c++)q[c]=(uint8_t)((q[c]*(255-p[3])+p[c]*p[3])/255);
    }
}
static int bonus52_begin(KBootstrap *b,int target,int result,int milliseconds){
    if(target<0||target>10000||milliseconds<0)return error(b,"bonus meter arguments");
    if(!b->layers[5].pixels||b->layers[5].width<1280||b->layers[5].height<1920)return error(b,"bonus meter atlas missing");
    static const char *names[]={"52_mater.rmt","52_mouse01.rmt","52_mouse02.rmt"};
    for(unsigned i=0;i<3;i++)if(!b->bonus52_ui[i].pixels){
        uint8_t *data=NULL;size_t n=0;
        if(read_named(&b->images,names[i],&data,&n)||rmt_decode(data,n,&b->bonus52_ui[i])){free(data);return error(b,"bonus meter resource");}free(data);
        if(b->bonus52_ui[i].width!=(i?32u:432u)||b->bonus52_ui[i].height!=(i?32u:24u))return error(b,"bonus meter dimensions");
    }
    b->bonus52_active=1;b->bonus52_target=target*100;b->bonus52_result=result;b->bonus52_limit=(uint64_t)milliseconds*60;
    b->bonus52_clock=b->bonus52_decay=0;b->bonus52_energy=b->bonus52_motion=0;
    b->bonus52_stage=b->vm->bytes[4011]%7;b->bonus52_pointer_valid=0;
    return 0;
}
static int bonus52_credits_begin(KBootstrap *b){
    if(b->layers[2].width<640||b->layers[2].height<960||b->layers[3].width<640||b->layers[3].height<480||b->layers[5].width<1280||b->layers[5].height<1280)return error(b,"bonus credits atlas missing");
    b->bonus52_active=2;b->bonus52_clock=0;b->bonus52_stage=0;return 0;
}
static void bonus52_credits_frame(KBootstrap *b){
    KImage *screen=&b->layers[0];
    if(b->bonus52_active==2){
        b->bonus52_clock+=1000;
        while(b->bonus52_clock>=1920){b->bonus52_clock-=1920;b->bonus52_stage++;}
        static const unsigned layer[]={2,3,5,5},start[]={480,1440,1920,2880},height[]={960,480,960,1280};
        for(unsigned y=0;y<480;y++){
            unsigned source=y+b->bonus52_stage;
            uint8_t *dst=screen->pixels+y*screen->stride;memset(dst,0,640*4);
            for(unsigned i=0;i<4;i++)if(source>=start[i]&&source<start[i]+height[i]){
                KImage *im=&b->layers[layer[i]];memcpy(dst,im->pixels+(source-start[i])*im->stride+(i==3?640*4:0),640*4);break;
            }
        }
        if(b->bonus52_stage>=3744){
            b->bonus52_active=3;b->bonus52_clock=0;
            if(b->music_active){b->music_fading=1;b->music_fade_clock=0;b->fade_db_step=(b->fade_db+5000)/192;if(b->fade_db_step<1)b->fade_db_step=1;}
        }
    }else{
        if(b->bonus52_active==3&&!b->music_active){b->bonus52_active=4;b->bonus52_clock=0;}
        if(b->bonus52_active==4)b->bonus52_clock+=1000;
        unsigned alpha=b->bonus52_active==3?255:b->bonus52_clock>=102000?0:255-(unsigned)(b->bonus52_clock/1200)*3;
        memset(screen->pixels,0,screen->stride*screen->height);
        for(unsigned y=0;y<416;y++)for(unsigned x=0;x<640;x++){
            uint8_t *dst=screen->pixels+y*screen->stride+x*4;const uint8_t *src=b->layers[5].pixels+(864+y)*b->layers[5].stride+(640+x)*4;
            for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*alpha/255);
            dst[3]=255;
        }
        if(!alpha)b->bonus52_active=0;
    }
}
static void bonus52_frame(KBootstrap *b){
    if(b->bonus52_active>=2){bonus52_credits_frame(b);return;}
    b->bonus52_clock+=1000;b->bonus52_decay+=1000;
    while(b->bonus52_decay>1200){b->bonus52_decay-=1200;b->bonus52_energy=b->bonus52_energy>2?b->bonus52_energy-2:0;}
    if(b->bonus52_motion>200){
        b->bonus52_motion=0;b->bonus52_energy+=100;
        b->bonus52_stage=(b->bonus52_stage+1)%7;
        if(!b->voice_active&&!b->voice_loading){
            static const char *voices[]={"a10234.ogg","a10240.ogg","a10246.ogg","a10229.ogg","a10234.ogg","a10240.ogg","a10229.ogg","a10229.ogg","a10234.ogg","a10240.ogg","a10240.ogg","a10229.ogg","a10234.ogg","a10240.ogg","a10234.ogg","a10229.ogg"};
            b->vm->random_state=b->vm->random_state*214013u+2531011u;
            b->audio_objects[2][0].state=1;
            snprintf(b->audio_objects[2][0].name,sizeof(b->audio_objects[2][0].name),"%s",voices[(b->vm->random_state>>16)&15]);
            if(voice_play(b))return;
        }
    }
    unsigned stage=b->bonus52_stage;KImage *atlas=&b->layers[5],*screen=&b->layers[0];
    unsigned sx=stage<4?0:640,sy=(stage<4?stage:stage-4)*480;
    for(unsigned y=0;y<480;y++)memcpy(screen->pixels+y*screen->stride,atlas->pixels+(sy+y)*atlas->stride+sx*4,640*4);
    bonus52_blend(screen,&b->bonus52_ui[0],104,424);
    bonus52_blend(screen,&b->bonus52_ui[1+(b->bonus52_clock/8000)%2],72,416);
    unsigned width=(unsigned)(b->bonus52_energy/100)*4;if(width>400)width=400;
    for(unsigned y=432;y<440;y++)for(unsigned x=120;x<120+width;x++){uint8_t *p=screen->pixels+y*screen->stride+x*4;p[0]=p[1]=0;p[2]=p[3]=255;}
    if(b->bonus52_energy>=b->bonus52_target){
        int result=b->bonus52_clock<=b->bonus52_limit?b->bonus52_result:10;
        b->vm->bytes[4011]=(uint8_t)b->bonus52_stage;b->bonus52_active=0;
        if(kvm_push(b->vm,(KValue){result,NULL}))error(b,"bonus meter result overflow");
    }
}

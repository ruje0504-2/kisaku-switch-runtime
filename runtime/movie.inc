static void movie_stop(KBootstrap *b){
    if(!b)return;
    kvideo_close(b->video);b->video=NULL;free(b->video_data);b->video_data=NULL;
    rmt_free(&b->movie_frame);
    free(b->mov_data);b->mov_data=NULL;memset(&b->mov,0,sizeof(b->mov));
    free(b->movie_pcm);b->movie_pcm=NULL;b->movie_pcm_size=b->movie_read=b->movie_clock=0;
    free(b->movie_effect.pcm);memset(&b->movie_effect,0,sizeof(b->movie_effect));b->movie_effect_name[0]=0;
    b->video_active=b->video_eof=b->video_background=b->video_wait=b->video_paused=b->video_change_wait=0;
    if(b->vm)b->vm->globals[0][50].number&=~0x2000;
}
static int movie_next(KBootstrap *b){
    for(unsigned guard=0;guard<1000;guard++){
        int event=kmov_next(&b->mov);
        if(!b->mov.changing)b->video_change_wait=0;
        if(event<0){int rc=error(b,b->mov.error);movie_stop(b);return rc;}
        if(event==0){
            /* A completed MOV no longer owns a playable decoder. Release the
               command/video/PCM buffers here instead of retaining them until
               the next movie or the whole bootstrap is destroyed. */
            movie_stop(b);return 0;
        }
        if(event==1){
            if(kvideo_range(b->video,b->mov.first,b->mov.last)){int rc=error(b,"MOV segment seek failed");movie_stop(b);return rc;}
            b->video_eof=0;b->video_active=1;b->vm->globals[0][50].number|=0x2000;return 0;
        }
        if(event==2&&*b->mov.audio&&option(b,"Effect","IsHEffect",1)){
            /* Version 2 embeds a sound name in each play command. Cache
               only while that resource is unchanged across loop boundaries. */
            if(!b->movie_effect.pcm||strcmp(b->movie_effect_name,b->mov.audio)){
                if(effect_decode(b,&b->movie_effect,b->mov.audio)){movie_stop(b);return -1;}
                snprintf(b->movie_effect_name,sizeof(b->movie_effect_name),"%s",b->mov.audio);
            }
            b->movie_effect.read=b->movie_effect.clock_position=0;
            if(b->movie_effect.pcm&&!b->movie_effect.loop_end)b->movie_effect.loop_end=b->movie_effect.size;
        }else{free(b->movie_effect.pcm);memset(&b->movie_effect,0,sizeof(b->movie_effect));}
    }
    {int rc=error(b,"MOV event budget exceeded");movie_stop(b);return rc;}
}
static int movie_open(KBootstrap *b,const char *name,unsigned entry){
    uint8_t *commands=NULL,*video_data=NULL;size_t commands_size=0,video_size=0;KMov mov;
    if(read_named(&b->movies,name,&commands,&commands_size))return error(b,"MOV resource missing");
    if(kmov_open(&mov,commands,commands_size,entry)){free(commands);return error(b,mov.error);}
    /* Native 488a80 calls 487870 before loading the next stream. In
       particular, do not create a second NVTEGRA decoder while the previous
       scene still owns its device, frame pool and loop cache. The new MOV
       commands are separate storage, so stopping the old stream is safe. */
    movie_stop(b);
    if(read_named(&b->movies,mov.video,&video_data,&video_size)){free(commands);return error(b,"MOV video missing");}
    KVideo *video=kvideo_open(video_data,video_size);
    if(!video){free(commands);free(video_data);return error(b,"MOV video open failed");}
    b->mov_data=commands;b->mov=mov;b->video=video;b->video_data=video_data;b->video_background=1;
    return movie_next(b);
}

static KImage *movie_target(KBootstrap *b){
    return b->choice_active?&b->choice_base:b->message_visible?&b->message_base:&b->layers[0];
}
static void movie_present(KBootstrap *b){
    KImage *target=movie_target(b);
    if(b->movie_frame.pixels)memcpy(target->pixels,b->movie_frame.pixels,640*480*4);
}
static void movie_frame(KBootstrap *b){
    if(!b->video_active)return;
    if(!b->movie_frame.pixels){
        uint8_t *pixels=malloc(640*480*4);
        if(!pixels){error(b,"movie frame allocation failed");movie_stop(b);return;}
        b->movie_frame=(KImage){0,0,640,480,2560,pixels};
        memcpy(pixels,movie_target(b)->pixels,640*480*4);
    }
    if(b->video_paused){movie_present(b);return;}
    if(b->movie_clock<b->movie_pcm_size){size_t n=b->movie_pcm_size-b->movie_clock;b->movie_clock+=n>2940?2940:n;}
    if(!b->video_eof){
        int rc=kvideo_step(b->video,&b->movie_frame,&b->movie_pcm,&b->movie_pcm_size);
        if(rc<0){error(b,kvideo_error(b->video));movie_stop(b);return;}
        b->video_eof=rc==1;
    }
    /* AX writes ordinary script pages; CVideoRender has its own decoded DIB
       (45b290/45af80). Reapply that frame after AX, including 60 Hz ticks on
       which the 30 fps decoder has no new frame, before composing messages.
       Copy the last frame BEFORE movie_next/stop can release its storage. */
    movie_present(b);
    if(b->video_eof&&b->movie_clock==b->movie_pcm_size){
        if(b->video_background){
            if(movie_next(b))return;
            /* EOF consumes no display tick: show the next range immediately. */
            if(b->video_active){
                int rc=kvideo_step(b->video,&b->movie_frame,&b->movie_pcm,&b->movie_pcm_size);
                if(rc<0){error(b,kvideo_error(b->video));movie_stop(b);return;}
                b->video_eof=rc==1;movie_present(b);
            }
        }else movie_stop(b);
    }
}

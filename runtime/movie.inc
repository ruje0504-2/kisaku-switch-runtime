static void movie_stop(KBootstrap *b){
    kvideo_close(b->video);b->video=NULL;free(b->video_data);b->video_data=NULL;
    free(b->mov_data);b->mov_data=NULL;memset(&b->mov,0,sizeof(b->mov));
    free(b->movie_pcm);b->movie_pcm=NULL;b->movie_pcm_size=b->movie_read=b->movie_clock=0;
    free(b->movie_effect.pcm);memset(&b->movie_effect,0,sizeof(b->movie_effect));
    b->video_active=b->video_eof=b->video_background=b->video_wait=b->video_paused=b->video_change_wait=0;
    b->vm->globals[0][50].number&=~0x2000;
}
static int movie_next(KBootstrap *b){
    for(unsigned guard=0;guard<1000;guard++){
        int event=kmov_next(&b->mov);
        if(!b->mov.changing)b->video_change_wait=0;
        if(event<0)return error(b,b->mov.error);
        if(event==0){
            free(b->movie_effect.pcm);memset(&b->movie_effect,0,sizeof(b->movie_effect));
            b->video_active=b->video_wait=0;b->vm->globals[0][50].number&=~0x2000;return 0;
        }
        if(event==1){
            if(kvideo_range(b->video,b->mov.first,b->mov.last))return error(b,"MOV segment seek failed");
            b->video_eof=0;b->video_active=1;b->vm->globals[0][50].number|=0x2000;return 0;
        }
        if(event==2&&*b->mov.audio&&option(b,"Effect","IsHEffect",1)){
            /* A MOV has one fixed sound resource. Rewind its decoded PCM at
               loop boundaries instead of reading/decoding the WAV again. */
            if(!b->movie_effect.pcm&&effect_decode(b,&b->movie_effect,b->mov.audio))return -1;
            b->movie_effect.read=b->movie_effect.clock_position=0;
            if(b->movie_effect.pcm&&!b->movie_effect.loop_end)b->movie_effect.loop_end=b->movie_effect.size;
        }else{free(b->movie_effect.pcm);memset(&b->movie_effect,0,sizeof(b->movie_effect));}
    }
    return error(b,"MOV event budget exceeded");
}
static int movie_open(KBootstrap *b,const char *name,unsigned entry){
    uint8_t *commands=NULL,*video_data=NULL;size_t commands_size=0,video_size=0;KMov mov;
    if(read_named(&b->movies,name,&commands,&commands_size))return error(b,"MOV resource missing");
    if(kmov_open(&mov,commands,commands_size,entry)){free(commands);return error(b,mov.error);}
    if(read_named(&b->movies,mov.video,&video_data,&video_size)){free(commands);return error(b,"MOV video missing");}
    KVideo *video=kvideo_open(video_data,video_size);
    if(!video){free(commands);free(video_data);return error(b,"MOV video open failed");}
    movie_stop(b);b->mov_data=commands;b->mov=mov;b->video=video;b->video_data=video_data;b->video_background=1;
    return movie_next(b);
}

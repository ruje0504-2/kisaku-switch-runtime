static void history_voice_stop(MessagePanel *p){
    kvoice_worker_cancel(p->voice_worker);free(p->voice_pcm);p->voice_pcm=NULL;
    p->voice_size=p->voice_at=0;p->voice_loading=0;p->voice_serial++;
}
static void history_voice_play(MessagePanel *p,KBootstrap *b){
    history_voice_stop(p);p->status[0]=0;
    const char *name=bootstrap_history_voice(b,p->back);
    if(!name){snprintf(p->status,sizeof(p->status),"This entry has no voice.");return;}
    int character=kisaku_voice_character(name);
    if(!bootstrap_message_setting(b,5,0)||(character>=0&&!bootstrap_message_setting(b,KSET_CHARACTER_VOICE+(unsigned)character,0))){snprintf(p->status,sizeof(p->status),"音声がオフになっています");return;}
    if(!p->voice_worker){
        char path[2048];int n=snprintf(path,sizeof(path),"%s/voice.arc",b->root);
        if(n<0||(size_t)n>=sizeof(path)||(p->voice_worker=kvoice_worker_create(path))==NULL){snprintf(p->status,sizeof(p->status),"Could not start voice decoder.");return;}
    }
    int volume=bootstrap_message_setting(b,2,0);
    if(volume<0||kvoice_worker_submit(p->voice_worker,name,pow(10.0,kisaku_sound_volume_db(volume,1)/2000.0))){snprintf(p->status,sizeof(p->status),"Could not load voice.");return;}
    p->voice_loading=1;snprintf(p->status,sizeof(p->status),"Loading voice...");
}
/* Called even while gameplay is paused. The independent worker cannot consume
   a pending result from the game's own voice channel. */
static int history_voice_audio(MessagePanel *p,KHistoryAudio *a,SDL_AudioDeviceID device,unsigned rate,unsigned channels,unsigned *serial){
    if(p->kind!=5){
        if(p->voice_loading||p->voice_pcm)history_voice_stop(p);
        if(p->kind==8)return 0; /* Settings owns the same preserved game queue. */
        return device?khistory_audio_restore(a,device):0;
    }
    if(!device||rate!=44100||channels!=2){
        if(p->voice_loading||p->voice_pcm){history_voice_stop(p);snprintf(p->status,sizeof(p->status),"Voice output unavailable.");}
        return 0;
    }
    if((p->voice_loading||p->voice_pcm)&&khistory_audio_suspend(a,device))return -1;
    if(a->suspended&&*serial!=p->voice_serial){SDL_PauseAudioDevice(device,1);SDL_ClearQueuedAudio(device);*serial=p->voice_serial;}
    if(p->voice_loading){
        int ready=kvoice_worker_poll(p->voice_worker,&p->voice_pcm,&p->voice_size);
        if(ready){if(ready<0){free(p->voice_pcm);p->voice_pcm=NULL;p->voice_size=0;}p->voice_loading=0;snprintf(p->status,sizeof(p->status),"%s",ready<0?"Voice could not be decoded.":"Playing voice...");}
    }
    while(p->voice_at<p->voice_size&&SDL_GetQueuedAudioSize(device)<8192){
        size_t n=p->voice_size-p->voice_at;if(n>4096)n=4096;
        if(SDL_QueueAudio(device,p->voice_pcm+p->voice_at,(Uint32)n))return -1;
        p->voice_at+=n;
    }
    if(p->voice_pcm){
        SDL_PauseAudioDevice(device,0);
        if(p->voice_at==p->voice_size&&!SDL_GetQueuedAudioSize(device)){free(p->voice_pcm);p->voice_pcm=NULL;p->voice_size=p->voice_at=0;snprintf(p->status,sizeof(p->status),"A / Enter: replay voice");}
    }
    return 0;
}

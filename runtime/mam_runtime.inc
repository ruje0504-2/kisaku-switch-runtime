/* Original MAM resources occupy one contiguous 940,054-byte span. Read it
   once at startup so starting a voice never adds small SD reads to rendering. */
static void mam_cache(KBootstrap *b){
    uint32_t first=UINT32_MAX;uint64_t last=0;
    for(unsigned i=0;i<b->data.count;i++){
        const Ai6Entry *e=&b->data.entries[i];size_t n=strlen(e->name);
        if(n<4||!equal(e->name+n-4,".mam"))continue;
        if(e->offset<first)first=e->offset;
        if((uint64_t)e->offset+e->packed>last)last=(uint64_t)e->offset+e->packed;
    }
    if(first==UINT32_MAX||last<=first||last-first>16*1024*1024)return;
    size_t size=(size_t)(last-first);uint8_t *data=malloc(size);if(!data)return;
    if(fseek(b->data.file,(long)first,SEEK_SET)||fread(data,1,size,b->data.file)!=size){free(data);return;}
    b->mam_archive=data;b->mam_archive_offset=first;b->mam_archive_size=size;
}
/* 443420: restore page 2, put the mouth from page 7 on page 4 using
   the native green key, then composite page 4 over the scene. */
static void mam_draw(KBootstrap *b){
    for(unsigned i=0;i<b->mam.count&&!b->error[0];i++){
        KMamTrack *t=&b->mam.tracks[i];if(!t->sequence||t->size<2)continue;
        unsigned frame=t->sequence[t->index];if(frame==255)continue;
        int32_t q[9]={(int32_t)t->dx,(int32_t)t->dy,(int32_t)t->width,(int32_t)t->height,0,(int32_t)t->dx,(int32_t)t->dy,2,1};
        if(blit_args(b,0,q))break;
        q[4]=4;q[5]=(int32_t)t->sx;q[6]=(int32_t)(t->sy+frame*t->height);q[7]=7;q[8]=0xff00;
        if(blit_args(b,3,q))break;
        q[4]=0;q[5]=(int32_t)t->dx;q[6]=(int32_t)t->dy;q[7]=4;
        if(blit_args(b,2,q))break;
    }
}
static void mam_stop(KBootstrap *b){
    if(b->mam_playing){for(unsigned i=0;i<b->mam.count;i++){b->mam.tracks[i].index=0;b->mam.tracks[i].active=0;}mam_draw(b);}
    b->mam_playing=0;
}
static int mam_prepare(KBootstrap *b,const char *voice){
    mam_stop(b);free(b->mam_data);b->mam_data=NULL;memset(&b->mam,0,sizeof(b->mam));
    if(!b->mouth_sheet_ready||!b->animation_name[0]||!voice[0]||
       tolower((unsigned char)voice[0])!=tolower((unsigned char)b->animation_name[0])||
       !option(b,"CONFIG","MAM",1)||!option(b,"Voice","IsVoice",1)||b->vm->bytes[8100]==3)return 0;
    char name[261];snprintf(name,sizeof(name),"%s",voice);char *dot=strrchr(name,'.');if(!dot)return 0;
    snprintf(dot,(size_t)(name+sizeof(name)-dot),".mam");
    uint8_t *data=NULL;size_t size=0;
    if(b->mam_archive)for(unsigned i=0;i<b->data.count;i++){
        const Ai6Entry *entry=&b->data.entries[i];if(!equal(entry->name,name))continue;
        if(entry->packed==entry->size&&entry->offset>=b->mam_archive_offset&&
           (uint64_t)entry->offset-b->mam_archive_offset+entry->size<=b->mam_archive_size){
            size=entry->size;data=malloc(size?size:1);if(!data)return error(b,"MAM allocation failed");
            memcpy(data,b->mam_archive+entry->offset-b->mam_archive_offset,size);
        }
        break;
    }
    if(!data&&read_named(&b->data,name,&data,&size))return 0;
    if(kmam_open(&b->mam,data,size)){free(data);return error(b,"MAM sequence invalid");}
    b->mam_data=data;b->mam_playing=1;return 0;
}
static void mam_frame(KBootstrap *b){
    if(!b->mam_playing)return;
    if(!b->voice_active){mam_stop(b);return;}
    if(b->voice_loading)return;
    b->mam.phase+=1000;
    while(b->mam.phase>=b->mam.period_ms*60){b->mam.phase-=b->mam.period_ms*60;kmam_tick(&b->mam);}
    mam_draw(b);
    unsigned active=0;for(unsigned i=0;i<b->mam.count;i++)active|=b->mam.tracks[i].active;
    if(!active)b->mam_playing=0;
}

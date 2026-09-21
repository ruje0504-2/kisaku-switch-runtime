#ifndef KISAKU_CONFIG_AUDIO_H
#define KISAKU_CONFIG_AUDIO_H
#include "bootstrap.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 4c2180/4c23c0/4c25e0: audition a playing channel, otherwise use the
   native sample. Modal playback has its own cursors; it cannot consume a
   story voice result or overwrite the story's decoded PCM. */
typedef struct {
    const uint8_t *pcm;size_t size,at,loop_start,loop_end;double gain;
} KConfigAudioSource;
typedef struct {
    uint8_t *samples[4];size_t sample_sizes[4];
    KConfigAudioSource sources[64];unsigned count,group,active,temporary,serial;
    unsigned output_owned;uint64_t until;
} KConfigAudio;
static void kconfig_audio_stop(KConfigAudio *a){
    a->active=a->count=a->temporary=0;a->until=0;a->serial++;
}
static void kconfig_audio_clear(KConfigAudio *a){
    for(unsigned i=0;i<4;i++)free(a->samples[i]);
    memset(a,0,sizeof(*a));
}
static int kconfig_audio_pending(const KConfigAudio *a){
    for(unsigned i=0;i<a->count;i++)if(a->sources[i].at<a->sources[i].size||a->sources[i].loop_end)return 1;
    return 0;
}
static void kconfig_audio_source(KConfigAudio *a,const uint8_t *pcm,size_t size,size_t at,size_t begin,size_t end,double gain){
    if(!pcm||size<4||a->count>=64||at>size)return;
    if(end>size||begin>=end||end-begin<4)begin=end=0;
    if(!end&&at==size)return;
    a->sources[a->count++]=(KConfigAudioSource){pcm,size-size%4,at-at%4,begin-begin%4,end-end%4,gain};
}
static int kconfig_audio_begin(KConfigAudio *a,KBootstrap *b,unsigned group){
    if(group>3)return -1;
    if(a->active&&a->group==group&&kconfig_audio_pending(a))return 0;
    kconfig_audio_stop(a);a->group=group;
    if(group==0&&b->music_active&&b->audio_rate==44100&&b->audio_channels==2)
        kconfig_audio_source(a,b->audio_pcm,b->audio_size,b->audio_cursor,b->audio_loop_start,b->audio_loop_end,b->music_gain);
    if(group==1||group==3)for(unsigned i=0;i<64;i++){
        if(group==1?i==3:i!=3)continue;
        KEffectTrack *t=&b->effect_tracks[i];
        kconfig_audio_source(a,t->pcm,t->size,t->clock_position,t->loop_start,t->loop_end,pow(10.0,-t->fade_attenuation/2000.0));
    }
    if(group==2){
        kconfig_audio_source(a,b->voice_pcm,b->voice_size,b->voice_clock_cursor,0,0,1);
        if(!a->count&&b->voice_active&&!b->music_active&&b->audio_rate==44100&&b->audio_channels==2)
            kconfig_audio_source(a,b->audio_pcm,b->audio_size,b->audio_cursor,0,0,1);
        /* An outstanding story decode must keep its original worker result. */
        if(!a->count&&b->voice_loading)return 0;
    }
    if(!a->count){
        static const char *names[]={"bgm01.wav","Akikaze.wav","z09588.ogg","Piss.wav"};
        if(!a->samples[group]){
            uint8_t *data=NULL,*pcm=NULL;size_t size=0,bytes=0;
            Ai6Archive *arc=group==0?&b->music:group==2?&b->voice:&b->effects;
            if(ai6_read_named(arc,names[group],&data,&size)||kaudio_decode(data,size,&pcm,&bytes)||!bytes||bytes%4){free(data);free(pcm);return -1;}
            free(data);a->samples[group]=pcm;a->sample_sizes[group]=bytes;
        }
        kconfig_audio_source(a,a->samples[group],a->sample_sizes[group],0,0,0,1);a->temporary=1;
    }
    a->active=a->count!=0;return 0;
}
static void kconfig_audio_release(KConfigAudio *a){
    /* 4c25e0 only stops sounds started for this audition. */
    if(a->temporary)kconfig_audio_stop(a);
    a->until=0;
}
static size_t kconfig_audio_read(KConfigAudio *a,const int *settings,uint8_t *out,size_t capacity){
    static const unsigned volumes[]={3,4,2,KSET_H_VOLUME},enabled[]={6,7,5,19};
    capacity-=capacity%4;if(!a->active||!capacity)return 0;
    double gain=settings[enabled[a->group]]?pow(10.0,kisaku_sound_volume_db(settings[volumes[a->group]],1)/2000.0):0;
    size_t total=0;
    for(size_t at=0;at<capacity;at+=4){
        int mixed[2]={0,0};unsigned present=0;
        for(unsigned i=0;i<a->count;i++){
            KConfigAudioSource *s=&a->sources[i];size_t end=s->loop_end?s->loop_end:s->size;
            if(s->at>=end){if(!s->loop_end)continue;s->at=s->loop_start;}
            if(s->at+4>end)continue;
            for(unsigned c=0;c<2;c++){const uint8_t *p=s->pcm+s->at+c*2;int sample=(int16_t)(p[0]|(unsigned)p[1]<<8);mixed[c]+=(int)(sample*gain*s->gain);}
            s->at+=4;present=1;
        }
        if(!present)break;
        for(unsigned c=0;c<2;c++){int value=mixed[c];if(value>32767)value=32767;if(value< -32768)value=-32768;out[at+c*2]=(uint8_t)value;out[at+c*2+1]=(uint8_t)((uint16_t)value>>8);}
        total+=4;
    }
    return total;
}
#endif

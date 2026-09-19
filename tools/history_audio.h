#ifndef KISAKU_HISTORY_AUDIO_H
#define KISAKU_HISTORY_AUDIO_H
#include <SDL.h>
#include <stdint.h>
#include <string.h>
/* SDL owns a copy of queued output. Retain its tail so a modal voice replay
   can suspend and restore exactly the unplayed game samples. */
typedef struct {
    uint8_t tail[16384],saved[16384];size_t count,saved_size;
    unsigned suspended;
} KHistoryAudio;
static inline int khistory_audio_queue(KHistoryAudio *a,SDL_AudioDeviceID device,const void *pcm,size_t size){
    if(size>sizeof(a->tail)||SDL_QueueAudio(device,pcm,(Uint32)size))return -1;
    if(a->count+size>sizeof(a->tail)){
        size_t keep=sizeof(a->tail)-size;
        memmove(a->tail,a->tail+a->count-keep,keep);a->count=keep;
    }
    memcpy(a->tail+a->count,pcm,size);a->count+=size;return 0;
}
static inline int khistory_audio_suspend(KHistoryAudio *a,SDL_AudioDeviceID device){
    if(a->suspended)return 0;
    SDL_PauseAudioDevice(device,1);size_t n=SDL_GetQueuedAudioSize(device);
    if(n>a->count||n>sizeof(a->saved))return -1;
    memcpy(a->saved,a->tail+a->count-n,n);a->saved_size=n;
    SDL_ClearQueuedAudio(device);a->suspended=1;return 0;
}
static inline int khistory_audio_restore(KHistoryAudio *a,SDL_AudioDeviceID device){
    if(!a->suspended)return 0;
    SDL_PauseAudioDevice(device,1);SDL_ClearQueuedAudio(device);
    if(a->saved_size&&SDL_QueueAudio(device,a->saved,(Uint32)a->saved_size))return -1;
    a->suspended=0;a->saved_size=0;return 0;
}
#endif

#include "voice_worker.h"
#include "ai6arc.h"
#include "video.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
struct KVoiceWorker {
    pthread_mutex_t mutex;pthread_cond_t condition;
#ifdef __SWITCH__
    Thread thread;
#else
    pthread_t thread;
#endif
    char path[2048],name[261];double gain;
    uint64_t generation;int stop,pending,ready,failed;
    uint8_t *pcm;size_t size;
};
static void work(KVoiceWorker *w){
    Ai6Archive arc={0};int opened=ai6_open(&arc,w->path)==0;
    pthread_mutex_lock(&w->mutex);
    while(!w->stop){
        while(!w->stop&&!w->pending)pthread_cond_wait(&w->condition,&w->mutex);
        if(w->stop)break;
        char name[261];strcpy(name,w->name);double gain=w->gain;uint64_t generation=w->generation;w->pending=0;
        pthread_mutex_unlock(&w->mutex);
        uint8_t *data=NULL,*pcm=NULL;size_t size=0,bytes=0;int failed=1;
        /* Loose override files (patches) take precedence over the archive. */
        if(opened)failed=ai6_read_named(&arc,name,&data,&size)||kaudio_decode(data,size,&pcm,&bytes);
        free(data);
        if(!failed)for(size_t i=0;i+1<bytes;i+=2){int16_t v=(int16_t)((unsigned)pcm[i]|((unsigned)pcm[i+1]<<8));v=(int16_t)(v*gain);pcm[i]=(uint8_t)v;pcm[i+1]=(uint8_t)((uint16_t)v>>8);}
        pthread_mutex_lock(&w->mutex);
        if(!w->stop&&generation==w->generation){w->pcm=pcm;w->size=bytes;w->failed=failed;w->ready=1;pcm=NULL;}
        free(pcm);
    }
    pthread_mutex_unlock(&w->mutex);ai6_close(&arc);
}
#ifdef __SWITCH__
static void entry(void *p){work(p);}
#else
static void *entry(void *p){work(p);return NULL;}
#endif
KVoiceWorker *kvoice_worker_create(const char *path){
    if(!path||strlen(path)>=2048)return NULL;
    KVoiceWorker *w=calloc(1,sizeof(*w));if(!w)return NULL;strcpy(w->path,path);
    if(pthread_mutex_init(&w->mutex,NULL)){free(w);return NULL;}
    if(pthread_cond_init(&w->condition,NULL)){pthread_mutex_destroy(&w->mutex);free(w);return NULL;}
#ifdef __SWITCH__
    /* Main rendering remains on its current core; decoding owns a second core. */
    int failed=R_FAILED(threadCreate(&w->thread,entry,w,NULL,1024*1024,0x2c,1));
    if(!failed&&R_FAILED(threadStart(&w->thread))){threadClose(&w->thread);failed=1;}
#else
    int failed=pthread_create(&w->thread,NULL,entry,w)!=0;
#endif
    if(failed){pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->mutex);free(w);return NULL;}return w;
}
void kvoice_worker_cancel(KVoiceWorker *w){if(!w)return;pthread_mutex_lock(&w->mutex);w->generation++;w->pending=w->ready=0;free(w->pcm);w->pcm=NULL;w->size=0;pthread_mutex_unlock(&w->mutex);}
int kvoice_worker_submit(KVoiceWorker *w,const char *name,double gain){
    if(!w||!name||!name[0]||strlen(name)>260||!(gain>=0&&gain<=1))return -1;
    pthread_mutex_lock(&w->mutex);w->generation++;strcpy(w->name,name);w->gain=gain;w->pending=1;w->ready=0;free(w->pcm);w->pcm=NULL;w->size=0;pthread_cond_signal(&w->condition);pthread_mutex_unlock(&w->mutex);return 0;
}
int kvoice_worker_poll(KVoiceWorker *w,uint8_t **pcm,size_t *size){
    *pcm=NULL;*size=0;if(!w)return -1;pthread_mutex_lock(&w->mutex);int result=0;
    if(w->ready){result=w->failed?-1:1;*pcm=w->pcm;*size=w->size;w->pcm=NULL;w->size=0;w->ready=0;}
    pthread_mutex_unlock(&w->mutex);return result;
}
void kvoice_worker_destroy(KVoiceWorker *w){if(!w)return;pthread_mutex_lock(&w->mutex);w->stop=1;pthread_cond_signal(&w->condition);pthread_mutex_unlock(&w->mutex);
#ifdef __SWITCH__
    threadWaitForExit(&w->thread);threadClose(&w->thread);
#else
    pthread_join(w->thread,NULL);
#endif
    free(w->pcm);pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->mutex);free(w);
}

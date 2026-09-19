#include "image_worker.h"
#include "ai6arc.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
struct KImageWorker {
    pthread_mutex_t mutex;pthread_cond_t condition;
#ifdef __SWITCH__
    Thread thread;
#else
    pthread_t thread;
#endif
    char path[2048],name[261];
    int stop,pending,busy,ready,failed;
    KImage image;
};
static void work(KImageWorker *w){
    Ai6Archive arc={0};int opened=ai6_open(&arc,w->path)==0;
    pthread_mutex_lock(&w->mutex);
    while(!w->stop){
        while(!w->stop&&!w->pending)pthread_cond_wait(&w->condition,&w->mutex);
        if(w->stop)break;
        char name[261];strcpy(name,w->name);w->pending=0;
        pthread_mutex_unlock(&w->mutex);
        uint8_t *data=NULL;size_t size=0;KImage image={0};int failed=1;
        /* Loose override files (patches) take precedence over the archive. */
        if(opened)failed=ai6_read_named(&arc,name,&data,&size)||rmt_decode(data,size,&image);
        free(data);
        pthread_mutex_lock(&w->mutex);
        if(!w->stop){w->image=image;w->failed=failed;w->ready=1;memset(&image,0,sizeof(image));}
        rmt_free(&image);
    }
    pthread_mutex_unlock(&w->mutex);ai6_close(&arc);
}
#ifdef __SWITCH__
static void entry(void *p){work(p);}
#else
static void *entry(void *p){work(p);return NULL;}
#endif
KImageWorker *kimage_worker_create(const char *path){
    if(!path||strlen(path)>=2048)return NULL;
    KImageWorker *w=calloc(1,sizeof(*w));if(!w)return NULL;strcpy(w->path,path);
    if(pthread_mutex_init(&w->mutex,NULL)){free(w);return NULL;}
    if(pthread_cond_init(&w->condition,NULL)){pthread_mutex_destroy(&w->mutex);free(w);return NULL;}
#ifdef __SWITCH__
    int failed=R_FAILED(threadCreate(&w->thread,entry,w,NULL,1024*1024,0x2c,2));
    if(!failed&&R_FAILED(threadStart(&w->thread))){threadClose(&w->thread);failed=1;}
#else
    int failed=pthread_create(&w->thread,NULL,entry,w)!=0;
#endif
    if(failed){pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->mutex);free(w);return NULL;}return w;
}
int kimage_worker_submit(KImageWorker *w,const char *name){
    if(!w||!name||!name[0]||strlen(name)>260)return -1;
    pthread_mutex_lock(&w->mutex);
    if(w->busy||w->stop){pthread_mutex_unlock(&w->mutex);return -1;}
    strcpy(w->name,name);w->pending=w->busy=1;pthread_cond_signal(&w->condition);pthread_mutex_unlock(&w->mutex);return 0;
}
int kimage_worker_poll(KImageWorker *w,KImage *image){
    memset(image,0,sizeof(*image));if(!w)return -1;
    pthread_mutex_lock(&w->mutex);int result=0;
    if(w->ready){result=w->failed?-1:1;*image=w->image;memset(&w->image,0,sizeof(w->image));w->ready=w->busy=0;}
    pthread_mutex_unlock(&w->mutex);return result;
}
void kimage_worker_destroy(KImageWorker *w){
    if(!w)return;
    pthread_mutex_lock(&w->mutex);w->stop=1;pthread_cond_signal(&w->condition);pthread_mutex_unlock(&w->mutex);
#ifdef __SWITCH__
    threadWaitForExit(&w->thread);threadClose(&w->thread);
#else
    pthread_join(w->thread,NULL);
#endif
    rmt_free(&w->image);pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->mutex);free(w);
}

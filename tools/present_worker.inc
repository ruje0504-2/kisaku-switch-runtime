/* During a job the worker owns presentation/choice/font state. The main
   thread may only mix audio and issue SDL calls until finish; it must finish
   before advancing the VM, handling input, changing settings or freeing b.
   No graphics API is called here. There is no frame queue or frame delay. */
#include <pthread.h>
typedef struct {
    pthread_mutex_t mutex;pthread_cond_t condition;
#ifdef __SWITCH__
    Thread thread;
#else
    pthread_t thread;
#endif
    int started,stop,busy,pending;
    KBootstrap *bootstrap;const KImage *source,*overlay;
} KPresentWorker;
static void present_worker_run(KPresentWorker *w){
    pthread_mutex_lock(&w->mutex);
    for(;;){
        while(!w->pending&&!w->stop)pthread_cond_wait(&w->condition,&w->mutex);
        if(w->stop)break;
        KBootstrap *b=w->bootstrap;w->pending=0;
        pthread_mutex_unlock(&w->mutex);
        const KImage *overlay=NULL,*source=bootstrap_present_layers(b,&overlay);
        pthread_mutex_lock(&w->mutex);
        w->source=source;w->overlay=overlay;w->busy=0;pthread_cond_broadcast(&w->condition);
    }
    pthread_mutex_unlock(&w->mutex);
}
#ifdef __SWITCH__
static void present_worker_entry(void *w){present_worker_run(w);}
#else
static void *present_worker_entry(void *w){present_worker_run(w);return NULL;}
#endif
static int present_worker_create(KPresentWorker *w){
    if(pthread_mutex_init(&w->mutex,NULL))return -1;
    if(pthread_cond_init(&w->condition,NULL)){pthread_mutex_destroy(&w->mutex);return -1;}
#ifdef __SWITCH__
    int failed=R_FAILED(threadCreate(&w->thread,present_worker_entry,w,NULL,1024*1024,0x2c,2));
    if(!failed&&R_FAILED(threadStart(&w->thread))){threadClose(&w->thread);failed=1;}
#else
    int failed=pthread_create(&w->thread,NULL,present_worker_entry,w)!=0;
#endif
    if(failed){pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->mutex);return -1;}
    w->started=1;return 0;
}
static int present_worker_submit(KPresentWorker *w,KBootstrap *b){
    if(!w->started||!b)return -1;
    pthread_mutex_lock(&w->mutex);
    if(w->busy||w->stop){pthread_mutex_unlock(&w->mutex);return -1;}
    w->bootstrap=b;w->source=w->overlay=NULL;w->busy=w->pending=1;
    pthread_cond_signal(&w->condition);pthread_mutex_unlock(&w->mutex);return 0;
}
static const KImage *present_worker_finish(KPresentWorker *w,const KImage **overlay){
    pthread_mutex_lock(&w->mutex);
    while(w->busy)pthread_cond_wait(&w->condition,&w->mutex);
    const KImage *source=w->source;*overlay=w->overlay;
    pthread_mutex_unlock(&w->mutex);return source;
}
static void present_worker_clear(KPresentWorker *w){
    if(!w->started)return;
    const KImage *overlay;present_worker_finish(w,&overlay);
    pthread_mutex_lock(&w->mutex);w->stop=1;pthread_cond_signal(&w->condition);pthread_mutex_unlock(&w->mutex);
#ifdef __SWITCH__
    threadWaitForExit(&w->thread);threadClose(&w->thread);
#else
    pthread_join(w->thread,NULL);
#endif
    pthread_cond_destroy(&w->condition);pthread_mutex_destroy(&w->mutex);memset(w,0,sizeof(*w));
}

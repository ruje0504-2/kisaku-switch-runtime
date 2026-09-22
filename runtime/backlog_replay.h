#ifndef KISAKU_BACKLOG_REPLAY_H
#define KISAKU_BACKLOG_REPLAY_H
#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {char (*names)[261];unsigned count;} KBacklogVoices;
static inline void kbacklog_voices_free(KBacklogVoices *v){free(v->names);memset(v,0,sizeof(*v));}
static inline int kbacklog_voice_add(KBacklogVoices *v,const char *name){
    if(!name||!name[0]||strlen(name)>260||v->count>=1024)return -1;
    void *p=realloc(v->names,(v->count+1)*sizeof(*v->names));if(!p)return -1;
    v->names=p;strcpy(v->names[v->count++],name);return 0;
}
typedef int (*KBacklogTextDraw)(void *,KVM *,unsigned,unsigned);
/* 4d8fe0 -> 46eb30 -> 4e4750 executes a recorded command buffer, with
   recording disabled. 4d9d30 / 4b6350 collect voices before serial playback.
   The isolated interpreter never shares writable VM memory or callbacks. */
static inline int kbacklog_replay(const uint8_t *data,size_t size,const KVM *source,
    KBacklogTextDraw draw,void *owner,KBacklogVoices *voices,char error[256]){
    if(!data||!size||size>262145||!source||(source->raw_size&&!source->raw)||!draw||!voices||!error)return -1;
    error[0]=0;KVM *v=kvm_create();uint8_t *code=malloc(size+4);KBacklogVoices staged={0};
    if(!v||!code){snprintf(error,256,"backlog allocation failed");free(code);kvm_destroy(v);return -1;}
    memcpy(v->globals,source->globals,sizeof(v->globals));memcpy(v->bytes,source->bytes,sizeof(v->bytes));memcpy(v->words,source->words,sizeof(v->words));
    v->byte_count=source->byte_count;v->word_count=source->word_count;memcpy(v->global_count,source->global_count,sizeof(v->global_count));
    v->random_state=source->random_state;
    if(source->raw_size){v->raw=malloc(source->raw_size);if(!v->raw)goto bad;memcpy(v->raw,source->raw,source->raw_size);v->raw_size=source->raw_size;}
    memset(code,0,4);memcpy(code+4,data,size);
    int module=kvm_add_module(v,"<backlog>",code,size+4);if(module<0||kvm_start(v,module))goto bad;
    const unsigned ids[]={30,31,42,43,44,45,46,47,49};
    const int values[]={16,18,32,0,592,54,32,0,6};
    for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);i++)v->globals[0][ids[i]]=(KValue){values[i],NULL};
    v->globals[0][50].number=(v->globals[0][50].number&~0x80)|0x400;
    unsigned width=16,height=16;int done=0;
    for(unsigned n=0;n<100000;n++){
        KStatus status=kvm_run(v,1);
        if(status==KVM_TEXT){if(draw(owner,v,width,height))goto bad;kvm_resume(v);}
        else if(status==KVM_SYSCALL){
            if(!v->sp||v->stack[v->sp-1].string)goto bad;
            int action=v->stack[v->sp-1].number;
            if(v->syscall==17&&action==5){
                if(v->sp<3||!v->stack[v->sp-2].string||v->stack[v->sp-3].string||v->stack[v->sp-3].number)goto bad;
                if(kbacklog_voice_add(&staged,v->stack[v->sp-2].string))goto bad;
                v->sp-=3;
            }else if(v->syscall==10&&action==0){
                if(v->sp<3||v->stack[v->sp-2].string||v->stack[v->sp-3].string)goto bad;
                int w=v->stack[v->sp-2].number,h=v->stack[v->sp-3].number;
                if(w<1||w>256||h<1||h>256)goto bad;
                width=(unsigned)w;height=(unsigned)h;v->sp-=3;
            }else {snprintf(error,256,"backlog syscall %d/%d unsupported at 0x%zx",v->syscall,action,v->instruction_ip);goto bad;}
            kvm_resume(v);
        }else if(status==KVM_BUDGET)kvm_resume(v);
        else if(status==KVM_YIELD||status==KVM_DONE){done=1;break;}
        else goto bad;
    }
    if(!done){snprintf(error,256,"backlog instruction limit");goto bad;}
    kbacklog_voices_free(voices);*voices=staged;free(v->raw);free(code);kvm_destroy(v);return 0;
bad:
    if(!error[0])snprintf(error,256,"backlog replay failed at 0x%zx: %.180s",v->instruction_ip,v->error);
    kbacklog_voices_free(&staged);free(v->raw);free(code);kvm_destroy(v);return -1;
}
#endif

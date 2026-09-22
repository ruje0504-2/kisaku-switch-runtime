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
/* A native CBackLog record is a byte vector terminated by a private zero
   sentinel.  The sentinel ends the individual record, but it is not a state
   reset: 4d8fe0 replays the selected row after the same text renderer has
   been configured by the preceding commands.  Keep this small descriptor
   independent of KMessageRecord so this header can remain included by
   bootstrap.h without a circular type dependency. */
typedef struct {const uint8_t *data;size_t size;} KBacklogReplayRecord;
typedef struct {KBacklogTextDraw draw;void *owner;size_t target_ip;} KBacklogReplayGate;
/* Record storage has a private trailing sentinel.  A completed VM record can
   also contain the terminal opcode 0 immediately before that sentinel; it
   must be removed when records are concatenated, while a text string's NUL
   terminator must stay in place. */
static inline size_t kbacklog_payload_size(const KBacklogReplayRecord *r){
    if(!r||!r->data||!r->size)return 0;
    size_t end=(r->data[r->size-1]==0)?r->size-1:r->size,p=0,last=SIZE_MAX;
    while(p<end){
        last=p;unsigned op=r->data[p++];
        if(op==0x0a||op==0x0b||op==0x33){const uint8_t *z=memchr(r->data+p,0,end-p);if(!z)return end;p=(size_t)(z-r->data)+1;}
        else if(op==0x14||op==0x15||op==0x16||op==0x19||op==0x1a||op==0x32)p+=4;
        else if(op==0x1b)p++;
        if(p>end)return end;
    }
    if(last!=SIZE_MAX&&r->data[last]==0&&last+1==end)return last;
    return end;
}
static inline int kbacklog_replay_gate(void *owner,KVM *v,unsigned width,unsigned height){
    KBacklogReplayGate *gate=owner;
    /* Earlier rows still run through the draw callback so cursor, font and
       rectangle globals carry into the selected row.  The caller can make
       that callback state-only before target_ip; voices remain gated below. */
    return gate->draw(gate->owner,v,width,height);
}
/* 4d8fe0 -> 46eb30 -> 4e4750 executes a recorded command buffer, with
   recording disabled. 4d9d30 / 4b6350 collect voices before serial playback.
   The isolated interpreter never shares writable VM memory or callbacks. */
static inline int kbacklog_replay_sequence(const KBacklogReplayRecord *records,unsigned count,
    unsigned target,const KVM *source,KBacklogTextDraw draw,void *owner,
    KBacklogVoices *voices,char error[256]){
    if(!records||!count||target>=count||!source||(source->raw_size&&!source->raw)||!draw||!voices||!error)return -1;
    /* A portable save caps the aggregate command payload at 16 MiB.  Replay
       the same bound so a corrupted record list cannot force an unbounded
       temporary module allocation. */
    /* KVM instruction_ip is relative to the payload after the four-byte
       message table header, so offsets here are payload-relative too. */
    size_t total=0,target_ip=0;
    for(unsigned i=0;i<count;i++){
        const KBacklogReplayRecord *r=&records[i];
        if(!r->data||!r->size||r->size>262145||total>UINT32_MAX-r->size)return -1;
        size_t payload=kbacklog_payload_size(r);
        size_t tail=i+1<count?0u:1u; /* only the final stream terminator */
        if(payload>SIZE_MAX-total-tail||total+payload+tail>16u*1024u*1024u)return -1;
        if(i<target)target_ip+=payload;
        total+=payload+tail;
    }
    error[0]=0;KVM *v=kvm_create();uint8_t *code=NULL;KBacklogVoices staged={0};
    if(!v){snprintf(error,256,"backlog allocation failed");return -1;}
    code=malloc(total+4);if(!code){snprintf(error,256,"backlog allocation failed");kvm_destroy(v);return -1;}
    memcpy(v->globals,source->globals,sizeof(v->globals));memcpy(v->bytes,source->bytes,sizeof(v->bytes));memcpy(v->words,source->words,sizeof(v->words));
    v->byte_count=source->byte_count;v->word_count=source->word_count;memcpy(v->global_count,source->global_count,sizeof(v->global_count));
    v->random_state=source->random_state;
    if(source->raw_size){v->raw=malloc(source->raw_size);if(!v->raw)goto bad;memcpy(v->raw,source->raw,source->raw_size);v->raw_size=source->raw_size;}
    memset(code,0,4);size_t at=4;
    for(unsigned i=0;i<count;i++){
        size_t payload=kbacklog_payload_size(&records[i]);
        memcpy(code+at,records[i].data,payload);at+=payload;
    }
    code[at++]=0;
    int module=kvm_add_module(v,"<backlog>",code,at);if(module<0||kvm_start(v,module))goto bad;
    const unsigned ids[]={30,31,42,43,44,45,46,47,49};
    const int values[]={16,18,32,0,592,54,32,0,6};
    for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);i++)v->globals[0][ids[i]]=(KValue){values[i],NULL};
    v->globals[0][50].number=(v->globals[0][50].number&~0x80)|0x400;
    unsigned width=16,height=16;int done=0,target_started=0;KBacklogReplayGate gate={draw,owner,target_ip};
    for(unsigned n=0;n<100000;n++){
        /* CBackLog resets only the selected record's cursor before executing
           that record.  instruction_ip still names the previous opcode after
           a TEXT or SYSCALL yield, so use the next instruction pointer and
           reset before its first opcode.  This leaves an explicit x/y store
           in the selected record authoritative while colour/font state
           carries over. */
        if(!target_started&&v->ip>=target_ip){
            v->globals[0][46]=(KValue){v->globals[0][42].number,NULL};
            v->globals[0][47]=(KValue){v->globals[0][43].number,NULL};
            target_started=1;
        }
        KStatus status=kvm_run(v,1);
        if(status==KVM_TEXT){if(kbacklog_replay_gate(&gate,v,width,height))goto bad;kvm_resume(v);}
        else if(status==KVM_SYSCALL){
            if(!v->sp||v->stack[v->sp-1].string)goto bad;
            int action=v->stack[v->sp-1].number;
            if(v->syscall==17&&action==5){
                if(v->sp<3||!v->stack[v->sp-2].string||v->stack[v->sp-3].string||v->stack[v->sp-3].number)goto bad;
                if(v->instruction_ip>=target_ip&&kbacklog_voice_add(&staged,v->stack[v->sp-2].string))goto bad;
                v->sp-=3;
            }else if(v->syscall==23&&action==8){
                /* CFuncBackLog::virtual_0(8) only queries the outer slot
                   vector count and discards the result.  The native call
                   has no operand or VM return value; consume the action
                   while retaining any values belonging to the recorded
                   caller. */
                v->sp--;
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
static inline int kbacklog_replay(const uint8_t *data,size_t size,const KVM *source,
    KBacklogTextDraw draw,void *owner,KBacklogVoices *voices,char error[256]){
    KBacklogReplayRecord record={data,size};
    return kbacklog_replay_sequence(&record,1,0,source,draw,owner,voices,error);
}
#endif

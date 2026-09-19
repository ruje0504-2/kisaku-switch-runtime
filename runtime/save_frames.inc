/* Portable extension for a MES called from inside a library function.
   The active MES still resumes through its native checkpoint preamble;
   only the suspended outer library frames require additional metadata. */
typedef struct {unsigned scripts,count,depths[64];KFrame frames[64];} KSavedFrames;
static int saved_frame_depth(const KVM *v,unsigned *out){
    if(!v||v->script_depth>64||v->depth>64)return -1;
    unsigned base=0;
    for(unsigned i=0;i<v->script_depth;i++){
        unsigned next=v->scripts[i].depth;
        if(next<base||next>v->depth)return -1;
        base=next;
    }
    for(unsigned i=0;i<base;i++){
        const KFrame *f=&v->frames[i];
        if(f->module<0||(unsigned)f->module>=v->module_count||f->argc>64||f->ip>INT32_MAX||f->ip>v->modules[f->module].size||!v->modules[f->module].boundaries[f->ip])return -1;
    }
    *out=base;return 0;
}
static KValue *saved_frame_values(const KVM *v,unsigned *count){
    unsigned base;if(saved_frame_depth(v,&base)||!base)return NULL;
    unsigned n=3+v->script_depth;for(unsigned i=0;i<base;i++)n+=3+v->frames[i].argc;
    if(n>8192)return NULL;
    KValue *values=calloc(n,sizeof(*values));if(!values)return NULL;
    values[0].number=1;values[1].number=(int32_t)v->script_depth;values[2].number=(int32_t)base;
    unsigned at=3;for(unsigned i=0;i<v->script_depth;i++)values[at++].number=(int32_t)v->scripts[i].depth;
    for(unsigned i=0;i<base;i++){
        const KFrame *f=&v->frames[i];values[at++].string=v->modules[f->module].name;
        values[at++].number=(int32_t)f->ip;values[at++].number=(int32_t)f->argc;
        memcpy(values+at,f->args,f->argc*sizeof(KValue));at+=f->argc;
    }
    *count=n;return values;
}
static int saved_frame_load(KBootstrap *b,const KControlRecord *records,unsigned count,unsigned scripts,KSavedFrames **out){
    *out=NULL;const KControlRecord *r=NULL;
    for(unsigned i=0;i<count;i++)if(records[i].type==0xfffc){if(r)return -1;r=&records[i];}
    if(!r)return 0;
    if(r->id||r->count<3||!scripts||scripts>64)return -1;
    for(unsigned i=0;i<3;i++)if(r->values[i].string)return -1;
    if(r->values[0].number!=1||r->values[1].number!=(int32_t)scripts||r->values[2].number<1||r->values[2].number>64||r->count<3+scripts)return -1;
    KSavedFrames *s=calloc(1,sizeof(*s));if(!s)return -1;s->scripts=scripts;s->count=(unsigned)r->values[2].number;
    unsigned at=3,previous=0;
    for(unsigned i=0;i<scripts;i++){
        KValue v=r->values[at++];if(v.string||v.number<0||(unsigned)v.number<previous||(unsigned)v.number>s->count)goto bad;
        previous=s->depths[i]=(unsigned)v.number;
    }
    if(previous!=s->count)goto bad;
    for(unsigned i=0;i<s->count;i++){
        if(r->count-at<3)goto bad;
        KValue name=r->values[at++],ip=r->values[at++],argc=r->values[at++];
        if(!name.string||!name.string[0]||name.number||ip.string||ip.number<0||argc.string||argc.number<0||argc.number>64||(unsigned)argc.number>r->count-at)goto bad;
        int id=module(b,name.string);if(id<0||(size_t)ip.number>b->vm->modules[id].size||!b->vm->modules[id].boundaries[ip.number])goto bad;
        KFrame *f=&s->frames[i];f->module=id;f->ip=(size_t)ip.number;f->argc=(unsigned)argc.number;
        memcpy(f->args,r->values+at,f->argc*sizeof(KValue));at+=f->argc;
    }
    if(at!=r->count)goto bad;
    *out=s;return 0;
bad:free(s);return -1;
}

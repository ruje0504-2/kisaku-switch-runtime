#ifndef KISAKU_BACKLOG_STORE_H
#define KISAKU_BACKLOG_STORE_H
/* Portable history extension v2, not a native FLAG format. Binary commands
   are hex encoded because KControlStore strings cannot contain NUL bytes. */
#define KBACKLOG_STORE_LIMIT (16u*1024u*1024u)
typedef struct KBacklogSnapshot {KMessageRecord *records;unsigned count;int index;unsigned recording;} KBacklogSnapshot;
static void kbacklog_snapshot_free(KBacklogSnapshot *s){
    if(!s)return;
    for(unsigned i=0;i<s->count;i++){free(s->records[i].data);free(s->records[i].text);}
    free(s->records);free(s);
}
static int kbacklog_hex(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int kbacklog_byte(const char *p){int a=kbacklog_hex((unsigned char)p[0]),b=kbacklog_hex((unsigned char)p[1]);return a<0||b<0?-1:a*16+b;}
static void kbacklog_put(char *p,unsigned n){static const char digits[]="0123456789abcdef";p[0]=digits[(n>>4)&15];p[1]=digits[n&15];}
static void kbacklog_store_free(KControlRecord *r){
    for(unsigned i=0;i<r->count;i++)free((void *)r->values[i].string);
    free(r->values);memset(r,0,sizeof(*r));
}
static int kbacklog_store_pack(const KBootstrap *b,KControlRecord *out){
    if(b->history_count>64||b->message_count>4096||b->message_index< -1||b->message_index>=(int)b->message_count||
       ((b->vm->globals[0][50].number&0x100)&&b->message_index<0))return -1;
    KControlRecord r={0,0xfffd,NULL,4+2*b->history_count+b->message_count};
    r.values=calloc(r.count,sizeof(*r.values));if(!r.values)return -1;
    r.values[0].number=2;r.values[1].number=(int)b->history_count;
    r.values[2].number=b->message_index;r.values[3].number=(b->vm->globals[0][50].number&0x100)!=0;
    size_t total=0;
    for(unsigned i=0;i<b->history_count;i++){
        unsigned at=(b->history_next+64-b->history_count+i)%64;
        const char *strings[]={b->history[at],b->history_voice[at]};
        for(unsigned j=0;j<2;j++){
            size_t n=strlen(strings[j])+1;char *p=malloc(n);if(!p)goto bad;
            memcpy(p,strings[j],n);r.values[4+i*2+j].string=p;total+=n;
        }
    }
    for(unsigned i=0;i<b->message_count;i++){
        const KMessageRecord *m=&b->messages[i];size_t text=m->text?strlen(m->text):0;
        if(m->size>262145||text>262144||(m->size&&(!m->data||m->data[m->size-1])))goto bad;
        size_t n=8+2*(m->size+text);
        if(n>1048576||total>KBACKLOG_STORE_LIMIT-n)goto bad;
        total+=n;char *p=malloc(n+1);if(!p)goto bad;
        KValue *v=&r.values[4+2*b->history_count+i];v->number=m->flag;v->string=p;
        for(unsigned j=0;j<4;j++)kbacklog_put(p+j*2,(unsigned)(m->size>>(j*8)));
        for(size_t j=0;j<m->size;j++)kbacklog_put(p+8+j*2,m->data[j]);
        for(size_t j=0;j<text;j++)kbacklog_put(p+8+2*(m->size+j),(unsigned char)m->text[j]);
        p[n]=0;
    }
    *out=r;return 0;
bad:kbacklog_store_free(&r);return -1;
}
/* Decode everything before changing the live runtime. Empty allocated slots,
   current index and active-record bit must survive, not only displayed rows. */
static int kbacklog_store_unpack(const KControlRecord *r,KBacklogSnapshot **out){
    if(r->count<4||r->values[0].string||r->values[0].number!=2)return -1;
    for(unsigned i=1;i<4;i++)if(r->values[i].string)return -1;
    int flat=r->values[1].number,index=r->values[2].number,active=r->values[3].number;
    if(flat<0||flat>64||r->count<4+2*(unsigned)flat||active<0||active>1)return -1;
    unsigned count=r->count-4-2*(unsigned)flat;
    if(count>4096||index< -1||index>=(int)count||(active&&index<0))return -1;
    KBacklogSnapshot *s=calloc(1,sizeof(*s));if(!s)return -1;
    s->records=calloc(count?count:1,sizeof(*s->records));if(!s->records){free(s);return -1;}
    s->count=count;s->index=index;s->recording=(unsigned)active;size_t total=0;
    for(unsigned i=0;i<count;i++){
        const KValue *v=&r->values[4+2*(unsigned)flat+i];const char *p=v->string;
        if(!p||v->number<0||v->number>255)goto bad;
        size_t n=strlen(p);if(n<8||(n&1)||n>1048576||total>KBACKLOG_STORE_LIMIT-n)goto bad;total+=n;
        uint32_t bytes=0;for(unsigned j=0;j<4;j++){int value=kbacklog_byte(p+j*2);if(value<0)goto bad;bytes|=(uint32_t)value<<(j*8);}
        size_t payload=(n-8)/2;if(bytes>262145||bytes>payload||payload-bytes>262144)goto bad;
        KMessageRecord *m=&s->records[i];m->flag=(uint8_t)v->number;
        m->data=malloc(bytes?bytes:1);m->text=malloc(payload-bytes+1);if(!m->data||!m->text)goto bad;
        m->capacity=m->size=bytes;m->text_capacity=payload-bytes+1;
        for(size_t j=0;j<payload;j++){
            int value=kbacklog_byte(p+8+j*2);if(value<0)goto bad;
            if(j<bytes)m->data[j]=(uint8_t)value;else {if(!value)goto bad;m->text[j-bytes]=(char)value;}
        }
        m->text[payload-bytes]=0;if(bytes&&m->data[bytes-1])goto bad;
    }
    *out=s;return 0;
bad:kbacklog_snapshot_free(s);return -1;
}
#endif

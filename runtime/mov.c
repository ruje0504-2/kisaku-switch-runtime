#include "mov.h"
#include <string.h>
#include <stdio.h>
static uint32_t u32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int fail(KMov *m,const char *s){snprintf(m->error,sizeof(m->error),"%s",s);return -1;}
static void select_entry(KMov *m){m->entry=m->requested;m->begin=m->ip=u32(m->data+530+m->entry*4);m->changing=m->depth=0;}
int kmov_open(KMov *m,const uint8_t *d,size_t n,unsigned entry){
    memset(m,0,sizeof(*m));
    if(!d||n<534||(u32(d)!=1&&u32(d)!=2)||!memchr(d+4,0,261)||!memchr(d+265,0,261))return fail(m,"MOV header invalid");
    unsigned count=u32(d+526);
    if(!count||count>256||entry>=count||(size_t)count*4>n-530)return fail(m,"MOV entry count invalid");
    unsigned stride=u32(d)==2?276:12;
    for(unsigned i=0;i<count;i++){size_t ip=u32(d+530+i*4);if(ip<530+count*4||ip>n||n-ip<stride)return fail(m,"MOV entry offset invalid");}
    m->data=d;m->size=n;m->count=count;m->requested=entry;m->video=(const char *)d+4;m->audio=(const char *)d+265;
    m->command_size=stride;
    select_entry(m);return 0;
}
int kmov_request(KMov *m,unsigned entry){if(!m->data||entry>=m->count)return fail(m,"MOV requested entry invalid");if(entry!=m->entry){m->requested=entry;m->changing=1;}return 0;}
int kmov_next(KMov *m){
    if(m->error[0])return -1;
    for(unsigned guard=0;guard<10000;guard++){
        if(m->ip>m->size||m->size-m->ip<m->command_size)return fail(m,"MOV command truncated");
        const uint8_t *p=m->data+m->ip;unsigned op=u32(p);int32_t a=(int32_t)u32(p+4),b=(int32_t)u32(p+8);m->ip+=m->command_size;
        switch(op){
        case 0:if(a<0||b<=a)return fail(m,"MOV segment range invalid");m->first=a;m->last=b;return 1;
        case 1:
            if(m->depth==64||a<0)return fail(m,"MOV loop count/depth invalid");
            m->loops[m->depth].ip=m->ip;m->loops[m->depth++].remaining=a-1;break;
        case 2:
            if(!m->depth)return fail(m,"MOV loop stack empty");
            if(!m->loops[m->depth-1].remaining){m->depth--;break;}
            if(m->loops[m->depth-1].remaining==-1&&m->changing){select_entry(m);break;}
            if(m->loops[m->depth-1].remaining>0)m->loops[m->depth-1].remaining--;
            m->ip=m->loops[m->depth-1].ip;break;
        case 3:
            if(m->changing){select_entry(m);return 3;}
            return 0;
        case 4:if(m->changing)select_entry(m);else m->ip=m->begin;break;
        case 5:
            if(m->command_size==276){
                if(!memchr(p+12,0,264))return fail(m,"MOV audio name unterminated");
                m->audio=(const char *)p+12;
            }
            return 2;
        default:return fail(m,"MOV command unknown");
        }
    }
    return fail(m,"MOV control flow budget exceeded");
}

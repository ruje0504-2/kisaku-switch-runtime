#include "scene.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "scene_transitions.inc"
static int same(const char *a,const char *b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return *a==*b;}
unsigned kscene_module(const char *name){
    if(!name||!*name)return 0;
    for(unsigned i=0;i<KSCENE_COUNT;i++)for(unsigned j=0;j<3;j++)if(same(name,scene_modules[i].names[j]))return scene_modules[i].id;
    return 0;
}
int kscene_edge(unsigned from,unsigned to){
    if(!from||!to||from>KSCENE_COUNT||to>KSCENE_COUNT)return -1;
    for(unsigned i=0;i<1000;i++)if(scene_edges[i].from==from&&scene_edges[i].to==to)return scene_edges[i].flag;
    return -1;
}
static void path_highlight(KScene *s){
    for(unsigned i=0;i<1000;i++)if(s->visited[i])s->visited[i]=1;
    for(unsigned i=1;i<s->path_count;i++){int edge=kscene_edge(s->counters[i-1],s->counters[i]);if(edge>=0)s->visited[edge]=2;}
}
int kscene_path_restore(KScene *s,const uint16_t *words,size_t count,const char *module){
    if(!s||!words||count<381)return -1;
    uint16_t path[KSCENE_COUNT]={0};unsigned n=0;
    for(;n<KSCENE_COUNT;n++){unsigned id=words[11+n]%1000;if(!id)break;if(id>KSCENE_COUNT)return -1;path[n]=(uint16_t)id;}
    if(!n){unsigned id=kscene_module(module);if(id)path[n++]=(uint16_t)id;}
    memcpy(s->counters,path,sizeof(path));s->path_count=n;path_highlight(s);return 0;
}
int kscene_transition(KScene *s,const char *from,const char *to,unsigned flag291){
    if(!s||!to||s->path_count>KSCENE_COUNT)return -1;
    if(same(to,"liblary.lib"))return 0;
    unsigned old=kscene_module(from),next=kscene_module(to);
    if(old==370&&same(to,"open.mes")){
        s->status[369]=6;memset(s->counters,0,sizeof(s->counters));s->path_count=0;path_highlight(s);return 1;
    }
    if(!next||old==next)return 0;
    int edge=kscene_edge(old,next);
    if((old&&next==1)||next==167||next==304||next==365||(edge<0&&(next==47||next==57||next==169))){
        memset(s->counters,0,sizeof(s->counters));s->path_count=0;
    }
    if(old&&s->status[old-1]!=6)s->status[old-1]=((old==246||old==251)&&!flag291)?2:1;
    unsigned n=0;while(n<s->path_count&&s->counters[n]!=next)n++;
    if(n==KSCENE_COUNT)return -1;
    s->counters[n++]=(uint16_t)next;s->path_count=n;
    memset(s->counters+n,0,(KSCENE_COUNT-n)*sizeof(*s->counters));path_highlight(s);return 1;
}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int compare(const void *a,const void *b){uint32_t x=((const KSceneNode *)a)->id,y=((const KSceneNode *)b)->id;return (x>y)-(x<y);}
int kscene_map(KScene *s,const uint8_t *data,size_t size){
    if(!s||!data||size<4)return -1;
    unsigned n=le32(data);if(n>KSCENE_COUNT||size!=4+(size_t)n*24)return -1;
    KSceneNode nodes[KSCENE_COUNT];
    for(unsigned i=0;i<n;i++){
        const uint8_t *p=data+4+i*24;
        nodes[i]=(KSceneNode){le32(p),le32(p+4),(int32_t)le32(p+8),(int32_t)le32(p+12),(int32_t)le32(p+16),(int32_t)le32(p+20)};
        if(nodes[i].id<1||nodes[i].id>KSCENE_COUNT||nodes[i].width<0||nodes[i].height<0)return -1;
    }
    /* 44d350 sorts unsigned IDs; 44c507 hides node 196's rectangle. */
    qsort(nodes,n,sizeof(*nodes),compare);
    for(unsigned i=0;i<n;i++){if(i&&nodes[i].id==nodes[i-1].id)return -1;if(nodes[i].id==196)nodes[i].width=nodes[i].height=0;}
    memcpy(s->nodes,nodes,n*sizeof(*nodes));s->count=n;return 0;
}
int kscene_restore(KScene *s,const uint8_t *bytes,size_t byte_count,const uint8_t *raw,size_t raw_count){
    if(!s||!bytes||byte_count<5370||!raw||raw_count<5402)return -1;
    uint32_t groups[KSCENE_COUNT]={0};
    /* 44c930: 20 rows, five 54-byte records in each 270-byte row.
       A nonzero LE16 at record+1 maps a one-based node to a one-based row. */
    for(unsigned row=0;row<20;row++)for(unsigned col=0;col<5;col++){
        const uint8_t *p=raw+row*270+col*54+1;unsigned id=p[0]|((unsigned)p[1]<<8);
        if(id>KSCENE_COUNT)return -1;
        if(id)groups[id-1]=row+1;
    }
    memcpy(s->visited,bytes+3000,1000);
    memcpy(s->status,bytes+4500,KSCENE_COUNT);memcpy(s->flags,bytes+5000,KSCENE_COUNT);
    memset(s->counters,0,sizeof(s->counters));s->path_count=0;memcpy(s->route_group,groups,sizeof(groups));return 0;
}

int kscene_complete(KScene *s,int scene,int part){
    static const uint8_t totals[KSCENE_COUNT]={
#include "scene_completion.inc"
    };
    if(!s||scene<1||scene>KSCENE_COUNT||part<0||part>255)return -1;
    unsigned slot=(unsigned)scene-1;
    s->flags[slot]=(uint8_t)(s->flags[slot]+part);
    if(s->flags[slot]==totals[slot])s->status[slot]=6;
    return 0;
}

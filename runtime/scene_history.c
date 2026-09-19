#include "scene_history.h"
#include "file_store.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
static const struct {uint16_t scene;uint8_t part;} catalog[600]={
#include "scene_catalog.inc"
};
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
int khistory_checkpoint(const KSceneHistory *h,unsigned slot,KSceneCheckpoint *out){
    if(!h||!h->loaded||slot>=600||!out||!catalog[slot].scene||!h->data[2404+slot])return -1;
    const uint8_t *p=h->data+3004+slot*32,*end=memchr(p,0,32);
    if(!end||end==p)return -1;
    for(const uint8_t *q=p;q<end;q++)if(!((*q>='a'&&*q<='z')||(*q>='A'&&*q<='Z')||(*q>='0'&&*q<='9')||*q=='_'||*q=='.'))return -1;
    const uint8_t *c=h->data+4+slot*4;
    KSceneCheckpoint next={catalog[slot].scene,catalog[slot].part,h->data[2404+slot],(unsigned)c[0]|(unsigned)c[1]<<8|(unsigned)c[2]<<16|(unsigned)c[3]<<24,{0}};
    memcpy(next.module,p,(size_t)(end-p));*out=next;return 0;
}
int khistory_load(KSceneHistory *h,const char *root){
    if(!h||!root)return -1;
    const char *files[]={"kisaku-scene.dat","save/SceneData"};
    for(unsigned i=0;i<2;i++){
        char path[2304];if(snprintf(path,sizeof(path),"%s/%s",root,files[i])>=(int)sizeof(path))return -1;
        if(!i&&kstore_recover(path))return -1;
        FILE *f=fopen(path,"rb");if(!f){if(errno==ENOENT)continue;return -1;}
        uint8_t data[KHISTORY_SIZE];size_t n=fread(data,1,sizeof(data),f);int end=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;
        if(bad||n!=sizeof(data)||end!=EOF||memcmp(data,"\0\6\21\5",4))return -1;
        /* Unvisited native slots can contain opaque non-string bytes. Keep
           them verbatim; only newly registered names are read as strings. */
        memcpy(h->data,data,sizeof(data));h->loaded=1;return 0;
    }
    memset(h,0,sizeof(*h));put32(h->data,0x05110600);h->loaded=1;return 0;
}
static int history_write(KSceneHistory *h,const char *root,const uint8_t next[KHISTORY_SIZE]){
    char path[2304],temp[2312];
    if(snprintf(path,sizeof(path),"%s/kisaku-scene.dat",root)>=(int)sizeof(path)||snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp))return -1;
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    int bad=fwrite(next,1,KHISTORY_SIZE,f)!=KHISTORY_SIZE;if(fclose(f))bad=1;
    if(bad||kstore_replace(temp,path)){remove(temp);return -1;}
    memcpy(h->data,next,KHISTORY_SIZE);return 0;
}
int khistory_register(KSceneHistory *h,const char *root,int scene,int part,const char *module,int32_t checkpoint){
    if(!h||!root||!module||strlen(module)>=32)return -1;
    unsigned slot=0;while(slot<600&&(catalog[slot].scene!=scene||catalog[slot].part!=part))slot++;
    if(slot==600)return -1;
    if(!h->loaded&&khistory_load(h,root))return -1;
    uint8_t next[KHISTORY_SIZE];memcpy(next,h->data,sizeof(next));
    /* 475d70 stores the current module and system variable 48. The separate
       completion byte array is unchanged. Preserve native fixed-slot tails. */
    memcpy(next+3004+slot*32,module,strlen(module)+1);put32(next+4+slot*4,(uint32_t)checkpoint);
    return history_write(h,root,next);
}

int khistory_completion(KSceneHistory *h,const char *root,int scene,int part,int value,int *previous){
    if(!h||!root||value< -1||value>255)return -1;
    unsigned slot=0;while(slot<600&&(catalog[slot].scene!=scene||catalog[slot].part!=part))slot++;
    if(slot==600||(!h->loaded&&khistory_load(h,root)))return -1;
    if(previous)*previous=h->data[2404+slot];
    if(value==-1||h->data[2404+slot]==value)return 0;
    uint8_t next[KHISTORY_SIZE];memcpy(next,h->data,sizeof(next));next[2404+slot]=(uint8_t)value;
    return history_write(h,root,next);
}

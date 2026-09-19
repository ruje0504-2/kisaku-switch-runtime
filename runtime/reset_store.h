#ifndef KISAKU_RESET_STORE_H
#define KISAKU_RESET_STORE_H
#include "file_store.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* A durable intent journal lets a multi-file reset finish after interruption.
   Validate the entire journal before replacing any owned file. */
typedef struct {const char *name;const void *data;size_t size;} KResetFile;
static inline uint32_t kreset_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void kreset_put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static inline int kreset_name(const char *s){
    if(strncmp(s,"kisaku-",6)||strstr(s,"reset-journal")||strlen(s)>=64)return 0;
    for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='.'))return 0;
    return 1;
}
static inline int kreset_recover(const char *root){
    char journal[2048];if(!root||snprintf(journal,sizeof(journal),"%s/kisaku-reset-journal.dat",root)>=(int)sizeof(journal)||kstore_recover(journal))return -1;
    FILE *f=fopen(journal,"rb");if(!f)return errno==ENOENT?0:-1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -1;}long length=ftell(f);
    if(length<12||length>64*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return -1;}
    uint8_t *data=malloc((size_t)length);if(!data){fclose(f);return -1;}
    int bad=fread(data,1,(size_t)length,f)!=(size_t)length||ferror(f);if(fclose(f))bad=1;
    KResetFile files[256];char names[256][64];unsigned count=bad?0:kreset_u32(data+8);size_t at=12;
    if(bad||memcmp(data,"K2RESET1",8)||!count||count>256)bad=1;
    for(unsigned i=0;!bad&&i<count;i++){
        if((size_t)length-at<68){bad=1;break;}
        memcpy(names[i],data+at,64);uint32_t size=kreset_u32(data+at+64);at+=68;
        if(!memchr(names[i],0,64)||!kreset_name(names[i])||size>(size_t)length-at){bad=1;break;}
        for(unsigned j=0;j<i;j++)if(!strcmp(names[i],names[j]))bad=1;
        files[i]=(KResetFile){names[i],data+at,size};at+=size;
    }
    if(at!=(size_t)length)bad=1;
    for(unsigned i=0;!bad&&i<count;i++){
        char path[2112],temp[2120];snprintf(path,sizeof(path),"%s/%s",root,files[i].name);snprintf(temp,sizeof(temp),"%s.tmp",path);
        f=fopen(temp,"wb");if(!f){bad=1;break;}
        bad=fwrite(files[i].data,1,files[i].size,f)!=files[i].size;if(fclose(f))bad=1;
        if(bad||kstore_replace(temp,path)){remove(temp);bad=1;}
    }
    free(data);
    /* Remove a leftover previous generation before removing the current
       intent, otherwise Switch recovery could resurrect an older reset. */
    if(!bad){char backup[2056];snprintf(backup,sizeof(backup),"%s.bak",journal);if(remove(backup)&&errno!=ENOENT)bad=1;}
    if(!bad&&remove(journal))bad=1;
    return bad?-1:0;
}
static inline int kreset_prepare(const char *root,const KResetFile *files,unsigned count){
    if(!root||strlen(root)>1800||!files||!count||count>256||kreset_recover(root))return -1;
    size_t total=12;
    for(unsigned i=0;i<count;i++){
        if(!files[i].name||!kreset_name(files[i].name)||(!files[i].data&&files[i].size)||files[i].size>64*1024*1024)return -1;
        for(unsigned j=0;j<i;j++)if(!strcmp(files[i].name,files[j].name))return -1;
        total+=68+files[i].size;if(total>64*1024*1024)return -1;
    }
    char path[2048],temp[2056];snprintf(path,sizeof(path),"%s/kisaku-reset-journal.dat",root);snprintf(temp,sizeof(temp),"%s.tmp",path);
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    uint8_t header[12]="K2RESET1";kreset_put32(header+8,count);int bad=fwrite(header,1,12,f)!=12;
    for(unsigned i=0;!bad&&i<count;i++){
        uint8_t entry[68]={0};strcpy((char *)entry,files[i].name);kreset_put32(entry+64,(uint32_t)files[i].size);
        bad=fwrite(entry,1,68,f)!=68||(files[i].size&&fwrite(files[i].data,1,files[i].size,f)!=files[i].size);
    }
    if(fclose(f))bad=1;
    if(bad||kstore_replace(temp,path)){remove(temp);return -1;}return 0;
}
#endif

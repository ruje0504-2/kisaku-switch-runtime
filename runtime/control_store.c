#include "control_store.h"
#include "file_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static int number(FILE *f,uint32_t *v){uint8_t p[4];if(fread(p,1,4,f)!=4)return -1;*v=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;return 0;}
static int putnum(FILE *f,uint32_t v){uint8_t p[4];for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));return fwrite(p,1,4,f)==4?0:-1;}
void kcontrol_free(KControlStore *s){if(!s)return;for(unsigned i=0;i<s->count;i++){for(unsigned j=0;j<s->records[i].count;j++)free((void *)s->records[i].values[j].string);free(s->records[i].values);}free(s);}
static KControlStore *read_file(FILE *f){
    if(fseek(f,0,SEEK_END))return NULL;
    long size=ftell(f);if(size<4||size>33554432||fseek(f,0,SEEK_SET))return NULL;
    KControlStore *s=calloc(1,sizeof(*s));if(!s)return NULL;
    uint32_t count;if(number(f,&count)||count>29)goto bad;
    for(unsigned i=0;i<count;i++){
        uint32_t key,n;if(number(f,&key)||number(f,&n)||n>8192)goto bad;
        if(i&&key<=((uint32_t)s->records[i-1].type<<16|s->records[i-1].id))goto bad;
        KControlRecord *r=&s->records[i];r->id=(uint16_t)key;r->type=(uint16_t)(key>>16);
        r->values=calloc(n?n:1,sizeof(KValue));if(!r->values)goto bad;r->count=n;s->count++;
        for(unsigned j=0;j<n;j++){
            uint32_t type,value,len;if(number(f,&type)||number(f,&value)||number(f,&len)||type>1||len>1048576||(!type&&len))goto bad;
            r->values[j].number=(int32_t)value;
            if(type){char *text=malloc((size_t)len+1);if(!text)goto bad;r->values[j].string=text;if(fread(text,1,len,f)!=len)goto bad;text[len]=0;if(memchr(text,0,len))goto bad;}
        }
    }
    if(fgetc(f)!=EOF||ferror(f))goto bad;
    return s;
bad:kcontrol_free(s);return NULL;
}
KControlStore *kcontrol_read(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    KControlStore *s=read_file(f);fclose(f);return s;
}
KControlStore *kcontrol_read_slot(const char *root,unsigned selector,unsigned slot){
    if(!root||selector>3||slot>999)return NULL;
    char path[4096];if(snprintf(path,sizeof(path),"%s/kisaku-control-%u-%03u.dat",root,selector,slot)>=(int)sizeof(path)||kstore_recover(path))return NULL;
    FILE *f=fopen(path,"rb");
    if(!f){
        if(errno!=ENOENT)return NULL;
        const char *dirs[]={"save","save52","save53","save54"};
        if(snprintf(path,sizeof(path),"%s/%s/SaveData%03u.dat",root,dirs[selector],slot)>=(int)sizeof(path))return NULL;
        f=fopen(path,"rb");if(!f)return NULL;
    }
    KControlStore *s=read_file(f);fclose(f);return s;
}
int kcontrol_write(const char *path,const KControlRecord *r,unsigned count){
    if(!path||count>29||(count&&!r)||strlen(path)>=4096)return -1;
    char temp[4100];
    snprintf(temp,sizeof(temp),"%s.tmp",path);FILE *f=fopen(temp,"wb");if(!f)return -1;
    int bad=putnum(f,count);
    for(unsigned i=0;!bad&&i<count;i++){
        uint32_t key=(uint32_t)r[i].type<<16|r[i].id;
        bad=r[i].count>8192||(r[i].count&&!r[i].values)||(i&&key<=((uint32_t)r[i-1].type<<16|r[i-1].id))||putnum(f,key)||putnum(f,r[i].count);
        for(unsigned j=0;!bad&&j<r[i].count;j++){
            KValue v=r[i].values[j];size_t n=v.string?strlen(v.string):0;
            bad=n>1048576||putnum(f,v.string!=NULL)||putnum(f,(uint32_t)v.number)||putnum(f,(uint32_t)n)||(n&&fwrite(v.string,1,n,f)!=n);
        }
    }
    if(fclose(f))bad=1;
    if(bad||kstore_replace(temp,path)){remove(temp);return -1;}return 0;
}

int kcontrol_write_slot(const char *root,unsigned selector,unsigned slot,const KControlRecord *r,unsigned count){
    if(!root||selector>3||slot>999)return -1;
    char path[4096];if(snprintf(path,sizeof(path),"%s/kisaku-control-%u-%03u.dat",root,selector,slot)>=(int)sizeof(path))return -1;
    return kcontrol_write(path,r,count);
}

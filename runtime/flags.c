#include "flags.h"
#include "file_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static int number(FILE *f,uint32_t *v){uint8_t p[4];if(fread(p,1,4,f)!=4)return -1;*v=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);return 0;}
static int bank(FILE *f,KFlags *s,unsigned b){
    uint32_t n;if(number(f,&n)||n>8192)return -1;
    s->globals[b]=calloc(n?n:1,sizeof(KValue));if(!s->globals[b])return -1;s->counts[b]=n;
    for(unsigned i=0;i<n;i++){
        uint32_t type,v,len;if(number(f,&type)||number(f,&v)||number(f,&len)||len>1048576||type>1)return -1;
        char *text=NULL;if(len){text=malloc((size_t)len+1);if(!text)return -1;if(fread(text,1,len,f)!=len){free(text);return -1;}text[len]=0;}
        if(type){if(!text){text=calloc(1,1);if(!text)return -1;}s->globals[b][i]=(KValue){(int32_t)v,text};}
        else {free(text);s->globals[b][i]=(KValue){(int32_t)v,NULL};}
    }return 0;
}
static int bytes(FILE *f,uint8_t **p,unsigned *count,unsigned max){uint32_t n;if(number(f,&n)||n>max)return -1;*p=malloc(n?n:1);if(!*p)return -1;*count=n;return fread(*p,1,n,f)==n?0:-1;}
KFlags *kflags_read(const char *path){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long size=ftell(f);
    if(size<260||size>33554432||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    KFlags *s=calloc(1,sizeof(*s));if(!s){fclose(f);return NULL;}
    uint32_t n;
    if(fread(s->module,1,260,f)!=260||bank(f,s,0)||bytes(f,&s->bytes,&s->byte_count,16384)||number(f,&n)||n>8192)goto bad;
    s->words=calloc(n?n:1,sizeof(uint16_t));if(!s->words)goto bad;s->word_count=n;
    for(unsigned i=0;i<n;i++){uint8_t p[2];if(fread(p,1,2,f)!=2)goto bad;s->words[i]=(uint16_t)(p[0]|(p[1]<<8));}
    if(bank(f,s,1)||bytes(f,&s->raw,&s->raw_count,16777216)||fgetc(f)!=EOF||ferror(f))goto bad;
    fclose(f);return s;
bad:fclose(f);kflags_free(s);return NULL;
}
void kflags_free(KFlags *s){if(!s)return;for(unsigned b=0;b<2;b++){for(unsigned i=0;i<s->counts[b];i++)free((void *)s->globals[b][i].string);free(s->globals[b]);}free(s->bytes);free(s->words);free(s->raw);free(s);}

static int putnum(FILE *f,uint32_t n){uint8_t p[4];for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));return fwrite(p,1,4,f)==4?0:-1;}
static int putbank(FILE *f,const KFlags *s,unsigned bank){
    if(s->counts[bank]>8192||putnum(f,s->counts[bank]))return -1;
    for(unsigned i=0;i<s->counts[bank];i++){
        KValue v=s->globals[bank][i];size_t n=v.string?strlen(v.string):0;
        if(n>1048576||putnum(f,v.string!=NULL)||putnum(f,(uint32_t)v.number)||putnum(f,(uint32_t)n)||(n&&fwrite(v.string,1,n,f)!=n))return -1;
    }return 0;
}
int kflags_write(const KFlags *s,const char *path){
    if(!s||!path||s->byte_count>16384||s->word_count>8192||s->raw_count>16777216)return -1;
    char temp[4096];if(snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp))return -1;
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    int bad=fwrite(s->module,1,260,f)!=260||putbank(f,s,0)||putnum(f,s->byte_count)||fwrite(s->bytes,1,s->byte_count,f)!=s->byte_count||putnum(f,s->word_count);
    for(unsigned i=0;!bad&&i<s->word_count;i++){uint8_t p[2]={(uint8_t)s->words[i],(uint8_t)(s->words[i]>>8)};bad=fwrite(p,1,2,f)!=2;}
    if(!bad)bad=putbank(f,s,1)||putnum(f,s->raw_count)||fwrite(s->raw,1,s->raw_count,f)!=s->raw_count;
    if(fclose(f))bad=1;
    if(bad||kstore_replace(temp,path)){remove(temp);return -1;}return 0;
}
int kflags_write_slot(const KFlags *f,const char *root,unsigned selector,unsigned slot){
    if(!f||!root||selector>3||slot>999)return -1;
    char path[4096];int n=slot?snprintf(path,sizeof(path),"%s/kisaku-flag-%u-%03u.dat",root,selector,slot):snprintf(path,sizeof(path),"%s/kisaku-flag-%u.dat",root,selector);
    return n>=(int)sizeof(path)?-1:kflags_write(f,path);
}
KFlags *kflags_read_slot(const char *root,unsigned selector,unsigned slot){
    if(!root||selector>3||slot>999)return NULL;
    char path[4096];
    {
        int n=slot?snprintf(path,sizeof(path),"%s/kisaku-flag-%u-%03u.dat",root,selector,slot):snprintf(path,sizeof(path),"%s/kisaku-flag-%u.dat",root,selector);
        if(n>=(int)sizeof(path))return NULL;
        if(kstore_recover(path))return NULL;
        FILE *f=fopen(path,"rb");if(f){fclose(f);return kflags_read(path);}if(errno!=ENOENT)return NULL;
    }
    const char *dirs[]={"save","save52","save53","save54"};
    if(snprintf(path,sizeof(path),"%s/%s/FLAG%03u",root,dirs[selector],slot)>=(int)sizeof(path))return NULL;
    FILE *native=fopen(path,"rb");
    if(native){fclose(native);return kflags_read(path);}
    if(errno!=ENOENT||selector||slot>201)return NULL;
    /* Portable first-use equivalent of FLAGINI.MES / HAGE_FLAGINI.MES:
       0..100 and 101..201, declared capacities at +e1 and clear at +110.
       Do not import the original installation's existing progress. */
    KFlags *fresh=calloc(1,sizeof(*fresh));if(!fresh)return NULL;
    fresh->byte_count=9192;fresh->word_count=600;fresh->counts[0]=51;
    fresh->counts[1]=100;fresh->raw_count=15000;
    fresh->bytes=calloc(fresh->byte_count,1);fresh->words=calloc(fresh->word_count,sizeof(uint16_t));
    fresh->globals[0]=calloc(fresh->counts[0],sizeof(KValue));
    fresh->globals[1]=calloc(fresh->counts[1],sizeof(KValue));fresh->raw=calloc(fresh->raw_count,1);
    if(!fresh->bytes||!fresh->words||!fresh->globals[0]||!fresh->globals[1]||!fresh->raw){kflags_free(fresh);return NULL;}
    strcpy((char *)fresh->module,slot<=100?"FLAGINI.MES":"HAGE_FLAGINI.MES");
    fresh->globals[0][8].number=(int32_t)(slot<=100?slot:slot-101);
    fresh->globals[0][29].number=515;
    fresh->globals[0][30].number=16;fresh->globals[0][31].number=20;
    fresh->globals[0][33].number=0xffffff;
    fresh->globals[0][34].number=16;fresh->globals[0][35].number=16;
    fresh->globals[0][36].number=1;fresh->globals[0][50].number=247;
    fresh->globals[1][61].number=slot>100;
    fresh->bytes[4008]=1;
    return fresh;
}
int kflags_progress(const char *root,unsigned selector,const uint8_t *bytes,unsigned count){
    if(!bytes)return -1;
    KFlags *s=kflags_read_slot(root,selector,0);if(!s)return -1;
    if(count<s->byte_count){kflags_free(s);return -1;}
    for(unsigned i=0;i<s->byte_count;i++)if(s->bytes[i]<bytes[i])s->bytes[i]=bytes[i];
    char path[4096];int rc=snprintf(path,sizeof(path),"%s/kisaku-flag-%u.dat",root,selector)>=(int)sizeof(path)?-1:kflags_write(s,path);kflags_free(s);return rc;
}
int kflags_merge(KFlags *saved,const KFlags *current){
    /* Validate everything before changing saved data. Native ECX operands at
       50812c/5081a4/508216 identify current as source and bank1[61] as mode. */
    if(!saved||!current||!saved->bytes||!current->bytes||!saved->words||!current->words||
       current->byte_count<saved->byte_count||current->byte_count<=3570||
       saved->word_count<100||current->word_count<100||current->counts[1]<=61||
       !current->globals[1]||current->globals[1][61].string)return -1;
    int alternate=current->globals[1][61].number!=0;
    int replace_group=current->bytes[3570]!=0;
    for(unsigned i=0;i<saved->byte_count;i++){
        uint8_t value=current->bytes[i];
        if(!alternate&&i>=3570&&i<=3594){if(replace_group)saved->bytes[i]=value;}
        else if(!alternate&&i>=3290&&i<=3599){if(value)saved->bytes[i]=value;}
        else if(alternate&&i>=3650&&i<=3711)saved->bytes[i]=value;
        else if(saved->bytes[i]<value)saved->bytes[i]=value;
    }
    for(unsigned i=32;i<100;i++)if(saved->words[i]<current->words[i])saved->words[i]=current->words[i];
    return 0;
}
int kflags_restore_progress(KFlags *slot,const KFlags *catalog,unsigned alternate){
    if(!slot||!catalog||!slot->bytes||!catalog->bytes||slot->byte_count!=9192||
       catalog->byte_count!=9192||alternate>1)return -1;
    /* 5079cf..507d41 reads the temporary FLAG100/201 as source. */
    if(!alternate){
        slot->bytes[1016]=catalog->bytes[1016];
        memcpy(slot->bytes+1022,catalog->bytes+1022,7);
        memcpy(slot->bytes+1032,catalog->bytes+1032,128);
    }
    for(unsigned i=2000;i<4096;i++)if(alternate||i!=4092)slot->bytes[i]=catalog->bytes[i];
    slot->bytes[4010]=slot->bytes[4011]=slot->bytes[4007]=0;
    memcpy(slot->bytes+5000,catalog->bytes+5000,2000);
    return 0;
}

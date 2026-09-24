#include "translation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int utf8_one(const unsigned char *p,size_t size,uint32_t *out,size_t *used){
    if(!p||!size||!out||!used)return -1;
    uint32_t cp=0,minimum=0;size_t n=0;
    if(p[0]<0x80){cp=p[0];n=1;}
    else if(p[0]>=0xc2&&p[0]<=0xdf){cp=p[0]&0x1f;n=2;minimum=0x80;}
    else if(p[0]>=0xe0&&p[0]<=0xef){cp=p[0]&0x0f;n=3;minimum=0x800;}
    else if(p[0]>=0xf0&&p[0]<=0xf4){cp=p[0]&7;n=4;minimum=0x10000;}
    else return -1;
    if(n>size)return -1;
    for(size_t i=1;i<n;i++){if((p[i]&0xc0)!=0x80)return -1;cp=(cp<<6)|(p[i]&0x3f);}
    if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return -1;
    *out=cp;*used=n;return 0;
}
static int parse_utf8(const char *text,uint32_t **out,size_t *count){
    size_t length=strlen(text),n=0;uint32_t *values=NULL;
    for(size_t at=0;at<length;){uint32_t cp;size_t step;if(utf8_one((const unsigned char *)text+at,length-at,&cp,&step)){free(values);return -1;}uint32_t *grown=realloc(values,(n+1)*sizeof(*grown));if(!grown){free(values);return -1;}values=grown;values[n++]=cp;at+=step;}
    *out=values;*count=n;return 0;
}
static uint64_t sequence_hash(const uint32_t *values,size_t count){
    uint64_t hash=1469598103934665603ULL^(uint64_t)count;
    for(size_t i=0;i<count;i++){hash^=values[i];hash*=1099511628211ULL;}
    return hash?hash:1;
}
static uint64_t sequence_hash_chars(const KTextChar *chars,size_t at,size_t count){
    uint64_t hash=1469598103934665603ULL^(uint64_t)count;
    for(size_t i=0;i<count;i++){hash^=chars[at+i].codepoint;hash*=1099511628211ULL;}
    return hash?hash:1;
}
static int entry_compare(const void *a,const void *b){
    const KTranslationEntry *x=a,*y=b;
    if(x->source_count!=y->source_count)return x->source_count>y->source_count?-1:1;
    for(size_t i=0;i<x->source_count;i++)if(x->source[i]!=y->source[i])return x->source[i]<y->source[i]?-1:1;
    return 0;
}
static int entry_matches(const KTranslationEntry *entry,const KTextChar *chars,size_t at,size_t count){
    if(entry->source_count>count-at)return 0;
    for(size_t i=0;i<entry->source_count;i++)if(entry->source[i]!=chars[at+i].codepoint)return 0;
    return 1;
}
static size_t next_power_two(size_t value){size_t n=1;while(n<value&&n<=SIZE_MAX/2)n<<=1;return n;}
static int build_buckets(KTranslation *translation){
    translation->bucket_count=next_power_two(translation->count*2+1);
    if(!translation->bucket_count)return -1;
    translation->buckets=malloc(translation->bucket_count*sizeof(*translation->buckets));
    if(!translation->buckets)return -1;
    for(size_t i=0;i<translation->bucket_count;i++)translation->buckets[i]=SIZE_MAX;
    for(size_t i=0;i<translation->count;i++){
        if(translation->entries[i].source_count>translation->max_source_count)translation->max_source_count=translation->entries[i].source_count;
        size_t slot=(size_t)sequence_hash(translation->entries[i].source,translation->entries[i].source_count)&(translation->bucket_count-1);
        while(translation->buckets[slot]!=SIZE_MAX){if(translation->buckets[slot]==i)break;slot=(slot+1)&(translation->bucket_count-1);}
        translation->buckets[slot]=i;
    }
    return 0;
}
int ktranslation_load(KTranslation *translation,const char *path){
    if(!translation||!path)return -1;
    ktranslation_free(translation);FILE *f=fopen(path,"rb");if(!f)return 0;
    char line[32768];int result=0;
    while(fgets(line,sizeof(line),f)){
        size_t length=strlen(line);while(length&&(line[length-1]=='\n'||line[length-1]=='\r'))line[--length]=0;
        if(!length||line[0]=='#')continue;
        char *tab=strchr(line,'\t');if(!tab||tab==line||!tab[1]){result=-1;break;}*tab=0;
        KTranslationEntry entry={0};
        if(parse_utf8(line,&entry.source,&entry.source_count)||parse_utf8(tab+1,&entry.target,&entry.target_count)||!entry.source_count||!entry.target_count){free(entry.source);free(entry.target);result=-1;break;}
        KTranslationEntry *grown=realloc(translation->entries,(translation->count+1)*sizeof(*grown));
        if(!grown){free(entry.source);free(entry.target);result=-1;break;}
        translation->entries=grown;translation->entries[translation->count++]=entry;
    }
    fclose(f);if(result){ktranslation_free(translation);return -1;}
    qsort(translation->entries,translation->count,sizeof(*translation->entries),entry_compare);
    if(build_buckets(translation)){ktranslation_free(translation);return -1;}
    return 0;
}
void ktranslation_free(KTranslation *translation){
    if(!translation)return;
    for(size_t i=0;i<translation->count;i++){free(translation->entries[i].source);free(translation->entries[i].target);}
    free(translation->entries);free(translation->buckets);translation->entries=NULL;translation->buckets=NULL;translation->count=translation->bucket_count=translation->max_source_count=0;
}
int ktranslation_apply(const KTranslation *translation,KTextChar *chars,size_t *count,size_t capacity){
    if(!translation||!chars||!count||*count>capacity)return -1;
    if(!translation->count)return 0;
    KTextChar *result=malloc(capacity*sizeof(*result));if(!result)return -1;
    size_t input_count=*count,at=0,out=0;
    while(at<input_count){
        const KTranslationEntry *best=NULL;size_t best_length=0;
        for(size_t length=1;length<=translation->max_source_count&&at+length<=input_count;length++){
            /* The lookup table includes the sequence length in its hash. */
            uint64_t exact=sequence_hash_chars(chars,at,length);
            size_t slot=(size_t)exact&(translation->bucket_count-1);
            for(size_t probes=0;probes<translation->bucket_count;probes++){
                size_t index=translation->buckets[slot];if(index==SIZE_MAX)break;
                const KTranslationEntry *entry=&translation->entries[index];
                if(entry->source_count==length&&sequence_hash(entry->source,length)==exact&&entry_matches(entry,chars,at,input_count)){best=entry;best_length=length;break;}
                slot=(slot+1)&(translation->bucket_count-1);
            }
        }
        if(best){if(out+best->target_count>capacity){free(result);return -1;}for(size_t j=0;j<best->target_count;j++)result[out++]=(KTextChar){best->target[j],best->target[j]<0x80?1:2};at+=best_length;}
        else{result[out++]=chars[at++];}
    }
    memcpy(chars,result,out*sizeof(*result));free(result);*count=out;return 0;
}

static const struct {const char *key,*value;} ui_defaults[]={
#include "ui_text_defaults.inc"
};
const char *kui_translation_default(const char *key){
    if(!key)return "";
    size_t lo=0,hi=sizeof(ui_defaults)/sizeof(*ui_defaults);
    while(lo<hi){
        size_t mid=lo+(hi-lo)/2;int order=strcmp(key,ui_defaults[mid].key);
        if(!order)return ui_defaults[mid].value;
        if(order<0)hi=mid;else lo=mid+1;
    }
    return key;
}
/* A loose translation cannot change printf's argument types or precision.
 * The current UI uses only fixed-width %s/%d/%u, with %% for literal percent. */
static int ui_format_matches(const char *source,const char *value){
    if(!strchr(source,'%'))return 1; /* Plain labels are never printf formats. */
    while((source=strchr(source,'%'))!=NULL){
        if(source[1]=='%'){source+=2;continue;}
        while((value=strchr(value,'%'))!=NULL&&value[1]=='%')value+=2;
        if(!value)return 0;
        const char *end=source+1;
        while(*end&&strchr("-+ #0.123456789",*end))end++;
        if(!*end||!strchr("sdu",*end))return 0;
        size_t n=(size_t)(end-source)+1;
        if(strncmp(source,value,n))return 0;
        source+=n;value+=n;
    }
    while((value=strchr(value,'%'))!=NULL){if(value[1]!='%')return 0;value+=2;}
    return 1;
}
static char *ui_unescape(const char *text){
    size_t length=strlen(text),out=0;char *copy=malloc(length+1);if(!copy)return NULL;
    for(size_t i=0;i<length;i++){
        if(text[i]=='\\'){
            if(i+1==length){free(copy);return NULL;}
            char next=text[++i];
            if(next=='n')copy[out++]='\n';
            else if(next=='r')copy[out++]='\r';
            else if(next=='t')copy[out++]='\t';
            else if(next=='\\')copy[out++]='\\';
            else{free(copy);return NULL;}
        }else copy[out++]=text[i];
    }
    copy[out]=0;
    for(size_t at=0;at<out;){uint32_t cp;size_t used;
        if(utf8_one((const unsigned char *)copy+at,out-at,&cp,&used)){free(copy);return NULL;}
        at+=used;
    }
    return copy;
}
int kui_translation_load(KUiTranslation *translation,const char *path){
    if(!translation||!path)return -1;
    kui_translation_free(translation);FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT?0:-1;
    char line[32768];int result=0,first=1;
    while(fgets(line,sizeof(line),f)){
        size_t length=strlen(line);
        if(length==sizeof(line)-1&&line[length-1]!='\n'){result=-1;break;}
        if(first&&length>=3&&!memcmp(line,"\xef\xbb\xbf",3)){memmove(line,line+3,length-2);length-=3;}
        first=0;
        while(length&&(line[length-1]=='\n'||line[length-1]=='\r'))line[--length]=0;
        if(!length||line[0]=='#')continue;
        char *tab=strchr(line,'\t');if(!tab||tab==line||!tab[1]){result=-1;break;}*tab=0;
        size_t key_length=strlen(line);
        if(key_length>127||strspn(line,"abcdefghijklmnopqrstuvwxyz0123456789_.")!=key_length||
           kui_translation_get(translation,line)){result=-1;break;}
        char *key=malloc(key_length+1),*value=ui_unescape(tab+1);
        if(key)memcpy(key,line,key_length+1);
        if(!key||!value||!ui_format_matches(kui_translation_default(line),value)){free(key);free(value);result=-1;break;}
        KUiTranslationEntry *grown=realloc(translation->entries,(translation->count+1)*sizeof(*grown));
        if(!grown){free(key);free(value);result=-1;break;}
        translation->entries=grown;translation->entries[translation->count++]=(KUiTranslationEntry){key,value};
    }
    if(ferror(f))result=-1;
    fclose(f);if(result){kui_translation_free(translation);return -1;}return 0;
}
void kui_translation_free(KUiTranslation *translation){
    if(!translation)return;
    for(size_t i=0;i<translation->count;i++){free(translation->entries[i].key);free(translation->entries[i].value);}
    free(translation->entries);translation->entries=NULL;translation->count=0;
}
const char *kui_translation_get(const KUiTranslation *translation,const char *key){
    if(!translation||!key)return NULL;
    for(size_t i=translation->count;i;i--)if(!strcmp(translation->entries[i-1].key,key))return translation->entries[i-1].value;
    return NULL;
}

#include "translation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static int entry_compare(const void *a,const void *b){
    const KTranslationEntry *x=a,*y=b;
    if(x->source[0]!=y->source[0])return x->source[0]<y->source[0]?-1:1;
    if(x->source_count!=y->source_count)return x->source_count>y->source_count?-1:1;
    for(size_t i=0;i<x->source_count&&i<y->source_count;i++)if(x->source[i]!=y->source[i])return x->source[i]<y->source[i]?-1:1;
    return 0;
}
static size_t first_lower(const KTranslation *translation,uint32_t codepoint){
    size_t lo=0,hi=translation->count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(translation->entries[mid].source[0]<codepoint)lo=mid+1;else hi=mid;}
    return lo;
}
static size_t first_upper(const KTranslation *translation,uint32_t codepoint){
    size_t lo=0,hi=translation->count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(translation->entries[mid].source[0]<=codepoint)lo=mid+1;else hi=mid;}
    return lo;
}
static int entry_matches(const KTranslationEntry *entry,const KTextChar *chars,size_t at,size_t count){
    if(entry->source_count>count-at)return 0;
    for(size_t i=0;i<entry->source_count;i++)if(entry->source[i]!=chars[at+i].codepoint)return 0;
    return 1;
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
        if(parse_utf8(line,&entry.source,&entry.source_count)||parse_utf8(tab+1,&entry.target,&entry.target_count)||!entry.source_count||entry.source_count!=entry.target_count){free(entry.source);free(entry.target);result=-1;break;}
        KTranslationEntry *grown=realloc(translation->entries,(translation->count+1)*sizeof(*grown));
        if(!grown){free(entry.source);free(entry.target);result=-1;break;}
        translation->entries=grown;translation->entries[translation->count++]=entry;
    }
    fclose(f);if(result){ktranslation_free(translation);return -1;}
    qsort(translation->entries,translation->count,sizeof(*translation->entries),entry_compare);return 0;
}
void ktranslation_free(KTranslation *translation){
    if(!translation)return;
    for(size_t i=0;i<translation->count;i++){free(translation->entries[i].source);free(translation->entries[i].target);}
    free(translation->entries);translation->entries=NULL;translation->count=0;
}
void ktranslation_apply(const KTranslation *translation,KTextChar *chars,size_t count){
    if(!translation||!translation->entries||!chars)return;
    for(size_t at=0;at<count;at++){
        size_t begin=first_lower(translation,chars[at].codepoint),end=first_upper(translation,chars[at].codepoint);
        for(size_t i=begin;i<end;i++){
            const KTranslationEntry *entry=&translation->entries[i];
            if(entry_matches(entry,chars,at,count)){for(size_t j=0;j<entry->target_count;j++)chars[at+j].codepoint=entry->target[j];at+=entry->target_count-1;break;}
        }
    }
}

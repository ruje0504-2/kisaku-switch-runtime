#ifndef KISAKU_MEDIA_TABLES_H
#define KISAKU_MEDIA_TABLES_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct {int32_t flag;const char *name;int32_t conditions[8];} KMediaRecord;
typedef struct {const char *name;int32_t flag,related_flag;} KMediaLink;
typedef struct {
    const KMediaRecord *records;size_t count;
    const KMediaLink *links;size_t link_count;
} KMediaTables;
/* Maps are represented by stable, ordered tables. Duplicate main keys retain
   all records; the auxiliary table uses its first matching key (503db0). */
static inline const KMediaRecord *kmedia_find(const KMediaTables *t,const char *name,size_t *cursor){
    if(!t||!name||!cursor)return NULL;
    for(;*cursor<t->count;){const KMediaRecord *r=&t->records[(*cursor)++];if(!strcmp(r->name,name))return r;}
    return NULL;
}
static inline const KMediaLink *kmedia_link(const KMediaTables *t,const char *name){
    if(!t||!name)return NULL;
    for(size_t i=0;i<t->link_count;i++)if(!strcmp(t->links[i].name,name))return &t->links[i];
    return NULL;
}
#endif

#include "gallery.h"
#include "file_store.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
static int same(const char *a,const char *b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return *a==*b;}
int kgallery_flush(KGallery *g,const char *root){
    if(!g->loaded||!g->dirty)return 0;
    char path[4096],temp[4100];if(snprintf(path,sizeof(path),"%s/kisaku-cg-%u.dat",root,g->selector)>=(int)sizeof(path))return -1;
    snprintf(temp,sizeof(temp),"%s.tmp",path);FILE *f=fopen(temp,"wb");if(!f)return -1;
    int bad=fwrite(g->flags,1,sizeof(g->flags),f)!=sizeof(g->flags);if(fclose(f))bad=1;
    if(bad||kstore_replace(temp,path)){remove(temp);return -1;}g->dirty=0;return 0;
}
int kgallery_load(KGallery *g,Ai6Archive *arc,const char *root,unsigned selector){
    if(selector>3)return -1;
    if(!g->initialized){
        uint8_t *data=NULL;size_t size=0;
        if(ai6_read_named(arc,"bmptbl.dat",&data,&size))return -1;
        if(!data||size!=KGALLERY_COUNT*16){free(data);return -1;}
        for(unsigned i=0;i<KGALLERY_COUNT;i++){
            const uint8_t *p=data+i*16;const uint8_t *end=memchr(p,0,14);unsigned id=p[14]|(unsigned)p[15]<<8;
            /* Variants intentionally share the native unlock ID. */
            if(!end||end==p||id>=KGALLERY_COUNT){free(data);return -1;}
            size_t n=(size_t)(end-p);memcpy(g->names[i],p,n);memcpy(g->names[i]+n,".rmt",5);g->ids[i]=(uint16_t)id;
        }
        free(data);data=NULL;size=0;
        if(ai6_read_named(arc,"cglist.dat",&data,&size))return -1;
        if(kgallery_catalog(g,data,size)){free(data);return -1;}free(data);g->initialized=1;
    }
    if(g->loaded&&g->selector==selector)return 0;
    if(kgallery_flush(g,root))return -1;
    uint8_t flags[KGALLERY_COUNT+1]={0};char path[4096];
    if(snprintf(path,sizeof(path),"%s/kisaku-cg-%u.dat",root,selector)>=(int)sizeof(path)||kstore_recover(path))return -1;
    FILE *f=fopen(path,"rb");
    if(!f){if(errno!=ENOENT)return -1;const char *dirs[]={"save","save52","save53","save54"};snprintf(path,sizeof(path),"%s/%s/checkflag.dat",root,dirs[selector]);f=fopen(path,"rb");if(!f&&errno!=ENOENT)return -1;}
    if(f){int bad=fread(flags,1,sizeof(flags),f)!=sizeof(flags)||fgetc(f)!=EOF||ferror(f);fclose(f);if(bad)return -1;}
    memcpy(g->flags,flags,sizeof(flags));g->loaded=1;g->selector=selector;g->dirty=0;return 0;
}
void kgallery_mark(KGallery *g,const char *name){
    if(!g->loaded)return;
    for(unsigned i=0;i<KGALLERY_COUNT;i++)if(same(g->names[i],name)){
        if(!g->flags[g->ids[i]]){g->flags[g->ids[i]]=g->flags[KGALLERY_COUNT]=1;g->dirty=1;}return;
    }
}

/* 4167d0: seven relative category offsets; category byte 1 is entry count.
   Entry: unlock ID u16, variant count u8, then triples of bmptbl indices. */
static unsigned catalog16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t catalog32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int kgallery_catalog(KGallery *g,const uint8_t *data,size_t size){
    if(!g||!data||size<28||size>sizeof(g->catalog))return -1;
    for(unsigned group=0;group<7;group++){
        size_t offset=catalog32(data+4*group);
        if(offset<28||offset>size-2)return -1;
        unsigned count=data[offset+1];if(!count||count>(size-offset-2)/4)return -1;
        for(unsigned item=0;item<count;item++){
            size_t entry=catalog32(data+offset+2+item*4);
            if(entry<28||entry>size-3)return -1;
            unsigned variants=data[entry+2];
            if(!variants&&catalog16(data+entry)==65535)continue; /* Native blank tile. */
            if(catalog16(data+entry)>=KGALLERY_COUNT||!variants||variants>(size-entry-3)/6)return -1;
            for(unsigned v=0;v<variants;v++)for(unsigned k=0;k<3;k++)
                if(catalog16(data+entry+3+v*6+k*2)>=KGALLERY_COUNT)return -1;
        }
    }
    memcpy(g->catalog,data,size);g->catalog_size=size;return 0;
}
unsigned kgallery_group_count(const KGallery *g,unsigned group){
    if(!g||!g->catalog_size||group>=7)return 0;
    size_t offset=catalog32(g->catalog+group*4);unsigned count=0;
    for(unsigned i=0;i<g->catalog[offset+1];i++)if(g->catalog[catalog32(g->catalog+offset+2+i*4)+2])count++;
    return count;
}
static const uint8_t *catalog_entry(const KGallery *g,unsigned group,unsigned item){
    if(!g||!g->catalog_size||group>=7)return NULL;
    size_t offset=catalog32(g->catalog+group*4);
    for(unsigned i=0;i<g->catalog[offset+1];i++){
        const uint8_t *entry=g->catalog+catalog32(g->catalog+offset+2+i*4);
        if(entry[2]){if(!item)return entry;item--;}
    }
    return NULL;
}
unsigned kgallery_variant_count(const KGallery *g,unsigned group,unsigned item){
    const uint8_t *entry=catalog_entry(g,group,item);return entry?entry[2]:0;
}
int kgallery_variant(const KGallery *g,unsigned group,unsigned item,unsigned variant,uint16_t images[3]){
    const uint8_t *entry=catalog_entry(g,group,item);if(!entry||!images||variant>=entry[2])return -1;
    for(unsigned k=0;k<3;k++)images[k]=(uint16_t)catalog16(entry+3+variant*6+k*2);
    return 0;
}
int kgallery_variant_unlocked(const KGallery *g,const uint8_t *flags,size_t size,const uint16_t images[3]){
    if(!g||!g->loaded||!images)return 0;
    for(unsigned k=0;k<3;k++)if(!k||images[k]){
        if(images[k]>=KGALLERY_COUNT||!g->flags[g->ids[images[k]]])return 0;
    }
    /* 41ab60: these compositions also require the scene-specific unlock. */
    static const uint16_t special[][2]={{0x8a,0x88},{0x8a,0x89},{0x23,0x24},{0x23,0x21},{0x1c,0},{0x1c,0x17},{0x216,0x217},{0x215,0x217},{0x215,0x21a},{0x216,0x21a},{0x218,0x21a},{0x213,0x217},{0x11b,0x11f},{0x20,0x21},{0x15,0x1a}};
    for(unsigned i=0;i<sizeof(special)/sizeof(*special);i++)if(images[0]==special[i][0]&&images[1]==special[i][1])return flags&&size>0xb95+i&&flags[0xb95+i]!=0;
    return 1;
}

/* 417575..418000: 14 entries per atlas page, native thumbnail/background
   coordinates, and alternate thumbnails for partially unlocked entries. */
static const struct {uint8_t group,raw,count;uint16_t ids[4];int8_t tiles[16];} thumb_rules[]={
#include "gallery_thumbnails.inc"
};
int kgallery_thumbnail(const KGallery *g,const uint8_t *flags,size_t size,unsigned group,unsigned item,KGalleryThumbnail *out){
    if(!g||!g->loaded||!out||group>=7||!g->catalog_size)return -1;
    unsigned images=kgallery_group_count(g,group);size_t off=catalog32(g->catalog+group*4);
    unsigned total=g->catalog[off+1],raw=0,logical=0;const uint8_t *entry=NULL;
    if(item<images){
        for(;raw<total;raw++){
            const uint8_t *r=g->catalog+catalog32(g->catalog+off+2+raw*4);
            if(r[2]&&logical++==item){entry=r;break;}
        }
        if(!entry)return -1;
    }else if(group==5&&item-images<14)raw=g->catalog[off]*14+item-images;
    else return -1;
    KGalleryThumbnail t={0};t.page=raw/14;t.index=raw%14;t.pages=g->catalog[off]+(group==5);t.tile=-1;
    if(entry){
        for(unsigned i=t.page*14;i<total&&i<(t.page+1)*14;i++)if(g->catalog[catalog32(g->catalog+off+2+i*4)+2])t.count++;
        if(g->flags[catalog16(entry)])t.tile=(int)t.index;
        else for(unsigned i=0;i<sizeof(thumb_rules)/sizeof(*thumb_rules);i++)if(thumb_rules[i].group==group&&thumb_rules[i].raw==raw){
            unsigned mask=0;for(unsigned k=0;k<thumb_rules[i].count;k++)if(g->flags[thumb_rules[i].ids[k]])mask|=1u<<k;
            t.tile=thumb_rules[i].tiles[mask];break;
        }
    }else{t.count=14;if(flags&&size>2950+t.index&&flags[2950+t.index])t.tile=(int)t.index;}
    if(t.index>=t.count||item<t.index)return -1;
    t.first=item-t.index;
    /* 4d9418..4d945d: sparse placement leaves room for page controls. */
    static const uint8_t p5[]={3,5,7,10,12},p7[]={3,5,6,7,9,10,12},p9[]={2,3,5,6,7,9,10,12,13},
        p10[]={2,3,5,6,7,9,10,11,12,13},p13[]={1,2,3,4,5,6,7,9,10,11,12,13,14},p14[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    const uint8_t *positions=t.count==5?p5:t.count==7?p7:t.count==9?p9:t.count==10?p10:t.count==13?p13:t.count==14?p14:NULL;
    if(!positions)return -1;
    unsigned pos=positions[t.index];t.x=40+(pos%4)*128;t.y=48+(pos/4)*98;
    snprintf(t.atlas,sizeof(t.atlas),"sp_cg_%c%02u.rmt","agefihk"[group],t.page+1);
    snprintf(t.background,sizeof(t.background),"sp_cg_bg%02u.rmt",t.count);*out=t;return 0;
}
unsigned kgallery_page_move(const KGallery *g,unsigned group,unsigned item,int direction){
    KGalleryThumbnail current,target;if(kgallery_thumbnail(g,NULL,0,group,item,&current))return item;
    unsigned page=(current.page+current.pages+(direction<0?-1:1))%current.pages;
    unsigned count=kgallery_group_count(g,group)+(group==5?14:0);
    for(unsigned i=0;i<count;i++)if(!kgallery_thumbnail(g,NULL,0,group,i,&target)&&target.page==page){
        unsigned index=current.index<target.count?current.index:target.count-1;return target.first+index;
    }
    return item;
}

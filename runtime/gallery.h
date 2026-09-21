#ifndef KISAKU_GALLERY_H
#define KISAKU_GALLERY_H
#include "ai6arc.h"
#define KGALLERY_COUNT 1336
typedef struct {
    char names[KGALLERY_COUNT][19];uint16_t ids[KGALLERY_COUNT];
    uint8_t flags[KGALLERY_COUNT+1];unsigned initialized,loaded,selector,dirty;
    uint8_t catalog[16384];size_t catalog_size;
} KGallery;
int kgallery_load(KGallery *g,Ai6Archive *data,const char *root,unsigned selector);
int kgallery_flush(KGallery *g,const char *root);
/* Mark a native CG resource. Returns 1 when the name belongs to bmptbl.dat,
 * 0 when it is not a gallery resource. */
int kgallery_mark(KGallery *g,const char *name);
int kgallery_catalog(KGallery *g,const uint8_t *data,size_t size);
unsigned kgallery_group_count(const KGallery *g,unsigned group);
unsigned kgallery_variant_count(const KGallery *g,unsigned group,unsigned item);
int kgallery_variant(const KGallery *g,unsigned group,unsigned item,unsigned variant,uint16_t images[3]);
int kgallery_variant_unlocked(const KGallery *g,const uint8_t *flags,size_t size,const uint16_t images[3]);
typedef struct {
    unsigned page,pages,index,count,first,x,y;int tile;
    char atlas[32],background[32];
} KGalleryThumbnail;
int kgallery_thumbnail(const KGallery *g,const uint8_t *flags,size_t size,unsigned group,unsigned item,KGalleryThumbnail *out);
unsigned kgallery_page_move(const KGallery *g,unsigned group,unsigned item,int direction);
#endif

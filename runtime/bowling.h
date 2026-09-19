#ifndef KISAKU_BOWLING_H
#define KISAKU_BOWLING_H
#include <stddef.h>
#include <stdlib.h>
/* CBowling's secondary this is primary this +0x9c. Slots correspond to
   secondary offsets 0x3c34..0x3ce4. Slots 17..22 are borrowed layer pointers
   (primary fields f45..f4a), not objects owned by the mini-game. */
typedef struct {void *object;void (*destroy)(void *);} KBowlingResource;
typedef struct KBowlingObject {
    struct KBowlingObject *next;
    KBowlingResource resource;
    void (*unregister)(void *context,void *object);
    void *context;
} KBowlingObject;
typedef struct KBowling {
    KBowlingResource slots[45];
    KBowlingObject *objects; /* secondary +3d14; nodes owned by this list */
    unsigned finish_state; /* secondary +3cec */
} KBowling;
static inline int kbowling_release(KBowling *b,KBowling **current){
    /* 4cfd20: retain native order, including 3cc8 before 3cc4. */
    static const unsigned order[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        23,24,25,26,27,28,29,30,31,32,33,34,35,37,36,38,39,40,41,42,43,44};
    if(!b||!current)return -1;
    /* Unknown object ownership is an error; do not partially release it. */
    for(unsigned i=0;i<sizeof(order)/sizeof(*order);i++){
        KBowlingResource *r=&b->slots[order[i]];
        if(r->object&&!r->destroy)return -1;
    }
    for(KBowlingObject *n=b->objects;n;n=n->next)
        if(!n->unregister||(n->resource.object&&!n->resource.destroy))return -1;
    for(unsigned i=0;i<sizeof(order)/sizeof(*order);i++){
        KBowlingResource *r=&b->slots[order[i]];
        if(r->object)r->destroy(r->object);
        *r=(KBowlingResource){0};
    }
    while(b->objects){
        KBowlingObject *n=b->objects;
        /* 4e21b0 unregisters before list removal and virtual destruction. */
        n->unregister(n->context,n->resource.object);
        b->objects=n->next;
        if(n->resource.object)n->resource.destroy(n->resource.object);
        free(n);
    }
    *current=NULL; /* 40cdb0(0) clears global 5c3b98 */
    b->finish_state=0;
    return 0;
}
#endif

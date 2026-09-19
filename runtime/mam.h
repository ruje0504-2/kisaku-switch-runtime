#ifndef KISAKU_MAM_H
#define KISAKU_MAM_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    const uint8_t *sequence;size_t size,index;
    unsigned remaining,active;
    uint32_t sx,sy,dx,dy,width,height;
} KMamTrack;
typedef struct {KMamTrack tracks[2];unsigned count,period_ms,phase;} KMam;
/* Borrow validated MAMP bytes until replaced. */
int kmam_open(KMam *m,const uint8_t *data,size_t size);
/* Advance one native timer tick; terminated tracks return to their first frame. */
void kmam_tick(KMam *m);
#endif

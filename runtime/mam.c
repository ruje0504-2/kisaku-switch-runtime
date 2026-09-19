#include "mam.h"
#include <string.h>
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int kmam_open(KMam *m,const uint8_t *data,size_t size){
    KMam result={0};
    if(!data||size<28||memcmp(data,"MAMP",4))return -1;
    unsigned rate=u32(data+4),count=u32(data+8);if(!rate||rate>1000||!count||count>2||size<28+count*44)return -1;
    result.period_ms=1000/rate;result.count=count;
    for(unsigned i=0;i<count;i++){
        const uint8_t *header=data+28+i*44;uint32_t frames=u32(header),length=u32(header+4),offset=u32(data+12+i*4);
        if(!frames||frames>255||!length||!(length&1)||offset<count*44||offset>size-28||length>size-28-offset)return -1;
        const uint8_t *sequence=data+28+offset;if(sequence[length-1]!=255)return -1;
        /* Native frame indices are byte values; some original tracks exceed
           the descriptive frame count. Rendering clips against the sheet. */
        KMamTrack *track=&result.tracks[i];track->sequence=sequence;track->size=length;
        track->sx=u32(header+12);track->sy=u32(header+16);track->dx=u32(header+20);track->dy=u32(header+24);track->width=u32(header+28);track->height=u32(header+32);
        if(!track->width||!track->height||track->sx>4096||track->sy>4096||track->dx>4096||track->dy>4096||track->width>4096||track->height>4096)return -1;
        track->active=length>1;track->remaining=length>1?sequence[1]:0;
    }
    *m=result;return 0;
}
void kmam_tick(KMam *m){
    for(unsigned i=0;i<m->count;i++){
        KMamTrack *t=&m->tracks[i];if(!t->active)continue;
        if(t->remaining)t->remaining--;
        if(!t->remaining){
            t->index+=2;
            if(t->index>=t->size||t->sequence[t->index]==255){t->index=0;t->active=0;}
            else t->remaining=t->sequence[t->index+1];
        }
    }
}

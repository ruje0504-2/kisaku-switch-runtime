#ifndef KISAKU_BOWLING_CPU_THROW_H
#define KISAKU_BOWLING_CPU_THROW_H
#include "bowling_player.h"
#include "bowling_throw.h"
#include <string.h>
typedef struct {
    KBowlingVector controls[3];
    double speed;
    unsigned approach_frames,approach_repeats;
    KBowlingActionStream release;
} KBowlingCPUThrow;
/* 4cb5e0: count six overlapping groups, retaining the first on ties. */
static inline unsigned kbowling_cpu_aim(const unsigned present[10]){
    static const unsigned mask[10]={1,3,5,10,23,36,8,26,52,32};
    unsigned count[6]={0},best=0;
    for(unsigned i=0;i<10;i++)if(present[i])for(unsigned j=0;j<6;j++)count[j]+=(mask[i]>>j)&1;
    for(unsigned j=1;j<6;j++)if(count[j]>count[best])best=j;
    return best;
}
/* 4cbd20..4cd230. same_rack means a remaining-pin throw, not ball #2:
   tenth-frame bonus balls may use a fresh rack. Preserve discarded random
   draws in characters 2/5 and float stores before adding perturbations. */
static inline int kbowling_cpu_throw(KBowlingCPUThrow *out,unsigned character,unsigned frame,unsigned same_rack,
    const unsigned present[10],const float aim[6][3],const float lanes[5][10][3],
    const KBowlingActionStream releases[9],uint32_t *rng){
    if(!out||!present||!aim||!lanes||!releases||!rng||character>8||frame>9||same_rack>1)return -1;
    static const double speeds[9]={6,8,9.2,6,5,10,6,12,9};
    static const unsigned frames[9]={7,8,8,10,8,7,8,8,8};
    static const unsigned repeats[9]={6,6,6,4,5,5,5,5,5};
    static const unsigned lane_index[6]={0,1,2,3,0,4};
    uint32_t state=*rng;
    KBowlingCPUThrow next={.speed=speeds[character],.approach_frames=frames[character],
        .approach_repeats=repeats[character],.release=releases[character]};
    float xs[3]={0},noise=0;
    const double small=(double).001f,step=(double).01f;
    if(character==0||character==1){
        if(!same_rack||character==0)noise=(float)((kbowling_rand(&state)%100)*small-(double).05f);
    }else if(character==2||character==5){
        noise=(float)((kbowling_rand(&state)%100)*(double).0008f-(character==2?(double).02f:step));
    }else if(character==3)noise=(float)((kbowling_rand(&state)%100)*step-.5);
    else if(character==4)noise=(float)((kbowling_rand(&state)%300)*step-1.5);
    else if(character==6){if(!same_rack)noise=(float)((kbowling_rand(&state)%50)*step-.25);}
    else if(character==7){if(!same_rack){noise=(float)((kbowling_rand(&state)%20)*small-step);xs[0]=.5f;xs[1]=.65f;xs[2]=(float)(noise+(double)-.35f);}}
    else if(!same_rack){xs[0]=-.6f;xs[1]=.6f;xs[2]=(float)(-(int)(kbowling_rand(&state)%100)*step);}
    if(same_rack)memcpy(xs,aim[kbowling_cpu_aim(present)],sizeof(xs));
    else if(character<6&&character!=4)memcpy(xs,lanes[lane_index[character]][frame],sizeof(xs));
    if(character<7&&(!same_rack||character==0||character==3||character==4)){
        xs[1]=(float)(xs[1]+(double)noise*.5);xs[2]=(float)((double)xs[2]+noise);
    }
    if(character==6)next.speed+=kbowling_rand(&state)%5;
    for(unsigned i=0;i<3;i++){
        if(!isfinite(xs[i]))return -1;
        next.controls[i]=(KBowlingVector){xs[i],0,(float)(23.288f*(double)i*.5)};
    }
    if(!next.release.commands||!next.release.count)return -1;
    *out=next;*rng=state;return 0;
}
#endif

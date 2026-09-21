#ifndef KISAKU_BOWLING_SESSION_H
#define KISAKU_BOWLING_SESSION_H
#include "bowling_match.h"
#include "bowling_cpu_throw.h"
#include "bowling_world.h"
typedef struct {
    const float (*positions)[3];
    const float (*aim)[3];
    const float (*lanes)[10][3];
    const KBowlingActionStream *releases;
    const uint8_t (*profiles)[21];
    const uint8_t *spreads;
} KBowlingTables;
typedef struct {
    KBowlingMatch match;
    KBowlingWorld world;
    KBowlingTables tables;
    uint32_t rng;
    unsigned difficulty,awaiting_score,same_rack;
} KBowlingSession;
/* 4cf750 uses a forward grouping pass, not a graph flood-fill. Preserve
   its order and head-pin early exit even for unusual remaining layouts. */
static inline unsigned kbowling_world_split(const KBowlingWorld *w){
    if(!w->fallen[0])return 0;
    unsigned remaining=0;
    for(unsigned i=1;i<10;i++)remaining+=!w->fallen[i];
    if(remaining==1)return 0;
    static const unsigned neighbors[9]={0,1,1,7,10,4,44,88,144};
    unsigned groups=0,assigned=0;
    for(unsigned i=0;i<9;i++)if(!w->fallen[i+1]){
        if(!(assigned&neighbors[i]))groups++;
        assigned|=1u<<i;
    }
    return groups>=2;
}
static inline int kbowling_session_create(KBowlingSession *s,int mode,const int32_t characters[4],
    unsigned difficulty,uint32_t seed,KBowlingTables tables){
    if(!s||!tables.positions||!tables.aim||!tables.lanes||!tables.releases||!tables.profiles||!tables.spreads||difficulty>2)return -1;
    KBowlingSession next={.tables=tables,.rng=seed,.difficulty=difficulty};
    if(kbowling_match_create(&next.match,mode,characters)||kbowling_world_rack(&next.world,tables.positions,difficulty,1))return -1;
    for(unsigned i=0;i<4;i++)if(characters[i]!=6&&!tables.spreads[characters[i]])return -1;
    *s=next;return 0;
}
static inline int kbowling_session_throw(KBowlingSession *s,const KBowlingVector controls[3],double speed){
    if(!s||!s->match.active||s->awaiting_score)return -1;
    if(kbowling_world_throw(&s->world,controls,speed))return -1;
    s->awaiting_score=1;return 0;
}
/* Planning never advances the shared RNG until the caller accepts the
   physical throw. Rendering can inspect the returned approach/release. */
static inline int kbowling_session_cpu_plan(const KBowlingSession *s,KBowlingCPUThrow *plan,uint32_t *next_rng){
    if(!s||!plan||!next_rng||!s->match.active||s->match.player!=1||!s->match.mode||s->awaiting_score)return -1;
    uint32_t random=s->rng;
    if(kbowling_cpu_throw(plan,s->match.characters[0],s->match.frame,s->same_rack,s->world.present,
        s->tables.aim,s->tables.lanes,s->tables.releases,&random))return -1;
    *next_rng=random;return 0;
}
static inline int kbowling_session_cpu_throw(KBowlingSession *s,KBowlingCPUThrow *plan){
    KBowlingCPUThrow next;uint32_t random;
    if(!plan||kbowling_session_cpu_plan(s,&next,&random))return -1;
    if(kbowling_session_throw(s,next.controls,next.speed))return -1;
    s->rng=random;*plan=next;return 0;
}
/* Called after the last physical tick and result/reaction presentation.
   No next-frame pins become visible before the frontend clears the old rack. */
static inline int kbowling_session_score(KBowlingSession *s){
    if(!s||!s->awaiting_score||s->world.active||!s->match.active)return -1;
    KBowlingSession next=*s;
    unsigned previous_frame=s->match.frame,previous_player=s->match.player;
    unsigned pins=kbowling_world_count(&s->world);
    unsigned split=s->match.ball==0?kbowling_world_split(&s->world):0;
    int result=kbowling_match_throw(&next.match,pins,split,s->world.gutter,s->tables.profiles,s->tables.spreads,&next.rng);
    if(result<0)return -1;
    next.awaiting_score=0;
    if(next.match.active){
        unsigned fresh=next.match.frame!=previous_frame||next.match.player!=previous_player;
        if(!fresh){
            unsigned ball=0;
            if(kbowling_score_next(&next.match.scores[previous_player],previous_frame,s->match.ball,&ball,&fresh)<0)return -1;
        }
        if(fresh){
            if(kbowling_world_rack(&next.world,s->tables.positions,s->difficulty,next.match.player==0))return -1;
        }else{
            kbowling_world_clear_fallen(&next.world);
            /* Native 4d3900 restores surviving pins to their rack origins. */
            for(unsigned i=0;i<10;i++)if(next.world.present[i])
                kbowling_pin_redirect(&next.world.pins[i],(KBowlingVector){s->tables.positions[i][0],s->tables.positions[i][1],s->tables.positions[i][2]},(KBowlingVector){0},0);
        }
        next.same_rack=!fresh;
    }
    *s=next;return result;
}
static inline int kbowling_session_second_half(KBowlingSession *s){
    if(!s||s->awaiting_score)return -1;
    KBowlingSession next=*s;
    if(kbowling_match_second_half(&next.match)||kbowling_world_rack(&next.world,s->tables.positions,s->difficulty,1))return -1;
    next.same_rack=0;*s=next;return 0;
}
#endif

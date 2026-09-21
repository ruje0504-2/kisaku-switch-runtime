#ifndef KISAKU_BOWLING_MATCH_H
#define KISAKU_BOWLING_MATCH_H
#include "bowling_player.h"
/* 4d6680 plays exactly five frames per modal invocation. Only player zero,
   and player one when mode!=0, enter the physical throw loop. Other players
   use 4cb870. The first modal returns to MES without a result on the stack. */
typedef struct {
    KBowlingScore scores[5];
    unsigned characters[4],mode,half,frame,player,ball,rack_remaining;
    unsigned initialized,active,first_finished,finished;
    int result;
} KBowlingMatch;
static inline int kbowling_match_create(KBowlingMatch *m,int mode,const int32_t characters[4]){
    if(!m||!characters)return -1;
    for(unsigned i=0;i<4;i++)if(characters[i]<0||characters[i]>8)return -1;
    KBowlingMatch next={.mode=mode!=0,.initialized=1,.active=1,.rack_remaining=10,.result=-1};
    for(unsigned i=0;i<4;i++)next.characters[i]=(unsigned)characters[i];
    for(unsigned i=0;i<5;i++)kbowling_score_reset(&next.scores[i]);
    *m=next;return 0;
}
static inline int kbowling_match_second_half(KBowlingMatch *m){
    if(!m||!m->initialized||m->active||!m->first_finished||m->finished||m->half)return -1;
    m->half=1;m->frame=5;m->player=m->ball=0;m->rack_remaining=10;m->active=1;return 0;
}
/* The caller supplies actual physical fallen pins, never a fabricated roll.
   Stage CPU updates and RNG too, so invalid metadata cannot partly advance a
   frame. Return 1 while awaiting another throw, 0 when this half is complete. */
static inline int kbowling_match_throw(KBowlingMatch *m,unsigned pins,unsigned split,unsigned gutter,
    const uint8_t profiles[9][21],const uint8_t spreads[9],uint32_t *rng){
    if(!m||!m->initialized||!m->active||!profiles||!spreads||!rng||m->frame>=10||m->player>=5||m->ball>=3||m->rack_remaining>10||pins>m->rack_remaining)return -1;
    for(unsigned i=0;i<4;i++)if(m->characters[i]>8||(m->characters[i]!=6&&!spreads[m->characters[i]]))return -1;
    KBowlingMatch next=*m;uint32_t random=*rng;
    if(kbowling_score_set(&next.scores[next.player],next.frame,next.ball,pins,split,gutter))return -1;
    unsigned ball=0,fresh=0;
    int more=kbowling_score_next(&next.scores[next.player],next.frame,next.ball,&ball,&fresh);
    if(more<0)return -1;
    if(more){
        next.ball=ball;next.rack_remaining=fresh?10:next.rack_remaining-pins;
    }else if(next.player==0&&next.mode){
        next.player=1;next.ball=0;next.rack_remaining=10;
    }else{
        for(unsigned player=next.mode?2:1;player<5;player++){
            unsigned role=next.characters[player-1];
            if(kbowling_cpu_frame(&next.scores[player],next.frame,role,profiles[role],spreads[role],&random))return -1;
        }
        next.frame++;next.player=next.ball=0;next.rack_remaining=10;
        if(next.frame==(next.half?10u:5u)){
            next.active=0;
            if(next.half){next.finished=1;next.result=kbowling_result(next.scores);if(next.result<0)return -1;}
            else next.first_finished=1;
        }
    }
    *m=next;*rng=random;return m->active?1:0;
}
#endif

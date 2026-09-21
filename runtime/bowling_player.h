#ifndef KISAKU_BOWLING_PLAYER_H
#define KISAKU_BOWLING_PLAYER_H
#include "bowling_score.h"
#include <stddef.h>
typedef struct {int16_t command;uint16_t delay;} KBowlingAction;
typedef struct {const KBowlingAction *commands;size_t count;} KBowlingActionStream;
typedef struct {
    KBowlingActionStream stream;
    size_t at;
    uint32_t due;
    unsigned active,released;
    int sprite;
} KBowlingActionPlayer;
static inline void kbowling_action_begin(KBowlingActionPlayer *p,KBowlingActionStream stream,uint32_t now){
    *p=(KBowlingActionPlayer){.stream=stream,.due=now,.active=1,.sprite=-1};
}
/* 500670 executes at most one instruction per native pump, even after a
   long delay. -3 latches release without advancing; -1 ends the stream. */
static inline int kbowling_action_tick(KBowlingActionPlayer *p,uint32_t now){
    if(!p->active)return 0;
    if(!p->stream.commands){p->active=0;return 0;}
    if(now<p->due)return 0;
    if(p->at>=p->stream.count)return -1;
    KBowlingAction action=p->stream.commands[p->at];
    if(action.command<-3)return -1;
    if(action.command==-3){p->released=1;return 0;}
    if(action.command==-1){p->active=0;return 0;}
    p->due=now+action.delay;p->at++;
    if(action.command==-2)return 0;
    p->sprite=action.command;return 1;
}
static inline unsigned kbowling_rand(uint32_t *state){
    *state=*state*UINT32_C(0x343fd)+UINT32_C(0x269ec3);
    return (*state>>16)&0x7fff;
}
/* 4cb240. Retain the native third-ball branch at 4cb4aa: when its sampled
   value is 1..9 it records the FIRST ball, rather than the sampled value. */
static inline int kbowling_cpu_frame(KBowlingScore *s,unsigned frame,unsigned character,const uint8_t profile[21],unsigned spread,uint32_t *rng){
    if(!s||!rng||!profile||frame>=10||character>8||(!spread&&character!=6))return -1;
    int a,b=0,c=15;
    if(character==6){
        a=(int)(kbowling_rand(rng)%10);
        if(frame<9&&a>1&&a<6)a=6;
        b=(int)(kbowling_rand(rng)%(10-a));
        kbowling_score_set(s,frame,0,(unsigned)a,0,0);
        kbowling_score_set(s,frame,1,(unsigned)b,0,0);
        if(frame==9)kbowling_score_set(s,frame,2,(unsigned)c,0,0);
        return 0;
    }
    a=profile[frame*2]-2+(int)(kbowling_rand(rng)%spread);
    unsigned split=0,gutter=a<1;
    if(a<1)a=0;
    else if(a>=10)a=10;
    else if(a>6&&a<9)split=(kbowling_rand(rng)&7)<3;
    kbowling_score_set(s,frame,0,(unsigned)a,split,gutter);
    if(a==10&&frame==9){
        b=profile[18]-2+(int)(kbowling_rand(rng)%spread);gutter=b<1;
        if(b<1)b=0;else if(b>=10)b=10;
    }else if(a<10){
        b=profile[frame*2+1]-2+(int)(kbowling_rand(rng)%spread);gutter=0;
        if(b<1)b=0;
        if(a+b>=10)b=10-a;
    }else gutter=0;
    kbowling_score_set(s,frame,1,(unsigned)b,0,gutter);
    if(frame==9&&a+b>=10){
        c=profile[20]-2+(int)(kbowling_rand(rng)%spread);gutter=0;
        if(a+b==10||b==10){
            if(c<1){c=0;gutter=1;}
            else if(c>=10)c=10;
            else c=a;
        }else{if(c<1)c=0;if(b+c>=10)c=10-b;}
        kbowling_score_set(s,frame,2,(unsigned)c,0,gutter);
    }
    return 0;
}
/* 4d6400 considers all players tied for the maximum to be first place. */
static inline int kbowling_result(const KBowlingScore scores[5]){
    for(unsigned i=0;i<5;i++)if(scores[i].total==UINT16_MAX)return -1;
    for(unsigned i=1;i<5;i++)if(scores[i].total>scores[0].total)return 0;
    return scores[0].total==300?2:1;
}
#endif

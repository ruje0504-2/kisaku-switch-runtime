#ifndef KISAKU_BOWLING_SCORE_H
#define KISAKU_BOWLING_SCORE_H
#include <stdint.h>
#include <string.h>
/* Native score record, 4ca500/4ca220: 30 packed rolls, ten frame scores,
   final total. 0xf/0xffff mean unresolved, not a zero-scoring throw. */
typedef struct {
    uint8_t rolls[10][3];
    uint16_t frames[10],total;
} KBowlingScore;
static inline void kbowling_score_reset(KBowlingScore *s){memset(s,0xff,sizeof(*s));}
static inline unsigned kbowling_score_pins(const KBowlingScore *s,unsigned frame,unsigned ball){
    return s->rolls[frame][ball]&15;
}
static inline int kbowling_score_set(KBowlingScore *s,unsigned frame,unsigned ball,unsigned pins,unsigned split,unsigned gutter){
    if(!s||frame>=10||ball>=3||(pins>10&&pins!=15)||(frame<9&&ball==2))return -1;
    s->rolls[frame][ball]=(uint8_t)((s->rolls[frame][ball]&0xc0)|pins|((split&1)<<4)|((gutter&1)<<5));
    for(unsigned i=0;i<10;i++)s->frames[i]=UINT16_MAX;
    /* 4ca220 leaves the previously completed total unchanged until every
       frame is resolved; preserve that detail when replacing a record. */
    for(unsigned i=0;i<9;i++){
        unsigned first=kbowling_score_pins(s,i,0),second,bonus=0;
        if(first==15)return 0;
        if(first==10){
            second=kbowling_score_pins(s,i+1,0);
            if(second==15)return 0;
            bonus=second==10&&i!=8?kbowling_score_pins(s,i+2,0):kbowling_score_pins(s,i+1,1);
            if(bonus==15)return 0;
        }else{
            second=kbowling_score_pins(s,i,1);
            if(second==15)return 0;
            if(first+second==10){bonus=kbowling_score_pins(s,i+1,0);if(bonus==15)return 0;}
        }
        s->frames[i]=(uint16_t)(first+second+bonus);
    }
    unsigned first=kbowling_score_pins(s,9,0),second=kbowling_score_pins(s,9,1),third=0;
    if(first==15||second==15)return 0;
    if(first==10||first+second==10){third=kbowling_score_pins(s,9,2);if(third==15)return 0;}
    s->frames[9]=(uint16_t)(first+second+third);s->total=0;
    for(unsigned i=0;i<10;i++)s->total=(uint16_t)(s->total+s->frames[i]);
    return 0;
}
/* 4d4970: a strike opens a fresh rack for ball two; ball three exists only
   after strike/spare and resets the rack after X,X or a spare. */
static inline int kbowling_score_next(const KBowlingScore *s,unsigned frame,unsigned ball,unsigned *next,unsigned *fresh){
    if(!s||!next||!fresh||frame>=10||ball>=3)return -1;
    unsigned a=kbowling_score_pins(s,frame,0),b=kbowling_score_pins(s,frame,1);
    if(kbowling_score_pins(s,frame,ball)==15)return -1;
    if(frame<9){if(ball||a==10)return 0;*next=1;*fresh=0;return 1;}
    if(ball==0){*next=1;*fresh=a==10;return 1;}
    if(ball==1&&(a==10||a+b==10)){*next=2;*fresh=b==10||(a!=10&&a+b==10);return 1;}
    return 0;
}
/* 4d2e20 glyph selection. Gutter=12, spare=11, strike=10; split is a
   separate overlay and must not change the score. */
static inline unsigned kbowling_score_glyph(const KBowlingScore *s,unsigned frame,unsigned ball){
    unsigned a=kbowling_score_pins(s,frame,0),b=kbowling_score_pins(s,frame,1),c=kbowling_score_pins(s,frame,2);
    unsigned pins=kbowling_score_pins(s,frame,ball),gutter=(s->rolls[frame][ball]>>5)&1;
    if(!ball)return gutter?12:a;
    if(frame<9)return a+b==10?11:b;
    if(ball==1){if(a==10)return gutter?12:b;return a+b==10?11:b;}
    if(a==10&&b!=10)return b+c==10?11:c;
    return gutter?12:pins;
}
#endif

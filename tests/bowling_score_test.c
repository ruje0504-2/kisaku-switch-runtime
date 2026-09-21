#include "bowling_score.h"
#include <assert.h>
#include <stdio.h>
static void put(KBowlingScore *s,unsigned f,unsigned b,unsigned p){assert(!kbowling_score_set(s,f,b,p,0,0));}
int main(void){
    KBowlingScore s;kbowling_score_reset(&s);assert(s.total==UINT16_MAX);
    unsigned next=99,fresh=99;
    assert(kbowling_score_next(&s,0,0,&next,&fresh)<0&&next==99&&fresh==99);
    for(unsigned f=0;f<10;f++){
        put(&s,f,0,10);
        if(f<9)assert(!kbowling_score_next(&s,f,0,&next,&fresh));
    }
    assert(s.frames[7]==30&&s.frames[8]==UINT16_MAX&&s.total==UINT16_MAX);
    assert(kbowling_score_next(&s,9,0,&next,&fresh)==1&&next==1&&fresh==1);
    put(&s,9,1,0);assert(kbowling_score_next(&s,9,1,&next,&fresh)==1&&!fresh);
    put(&s,9,1,10);assert(s.frames[8]==30&&s.total==UINT16_MAX);
    assert(kbowling_score_next(&s,9,1,&next,&fresh)==1&&next==2&&fresh==1);
    put(&s,9,2,10);assert(s.total==300&&!kbowling_score_next(&s,9,2,&next,&fresh));
    kbowling_score_reset(&s);
    for(unsigned f=0;f<10;f++){put(&s,f,0,5);put(&s,f,1,5);assert(kbowling_score_glyph(&s,f,1)==11);}
    assert(s.total==UINT16_MAX);put(&s,9,2,5);assert(s.total==150);
    kbowling_score_reset(&s);
    for(unsigned f=0;f<10;f++){put(&s,f,0,3);put(&s,f,1,4);}
    assert(s.total==70&&!kbowling_score_next(&s,9,1,&next,&fresh));
    /* Last-frame X,7,/ uses the same rack for its third ball. */
    put(&s,9,0,10);put(&s,9,1,7);
    assert(kbowling_score_next(&s,9,1,&next,&fresh)==1&&next==2&&!fresh);
    put(&s,9,2,3);assert(s.total==83&&kbowling_score_glyph(&s,9,2)==11);
    /* Explicit spare opens a fresh rack for the bonus ball. */
    put(&s,9,0,7);put(&s,9,1,3);
    assert(kbowling_score_next(&s,9,1,&next,&fresh)==1&&fresh);
    put(&s,9,2,10);assert(s.total==83&&kbowling_score_glyph(&s,9,2)==10);
    assert(!kbowling_score_set(&s,0,0,0,1,1));
    assert((s.rolls[0][0]&0x30)==0x30&&kbowling_score_glyph(&s,0,0)==12);
    KBowlingScore before=s;assert(kbowling_score_set(&s,10,0,0,0,0)<0);
    assert(kbowling_score_set(&s,0,2,0,0,0)<0&&kbowling_score_set(&s,0,0,11,0,0)<0);
    assert(!memcmp(&s,&before,sizeof(s)));
    puts("CBowling native scoring: pending bonuses, 300/150/open games, tenth-frame racks, glyphs and bounds: PASS");
    return 0;
}

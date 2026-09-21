#include "bowling_turn.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    KBowlingTables tables={kisaku_bowling_pin_positions,kisaku_bowling_aim,kisaku_bowling_lanes,kisaku_bowling_throws,kisaku_bowling_profiles,kisaku_bowling_spread};
    int32_t roles[4]={0,1,2,3};KBowlingSession s;KBowlingTurn t;
    assert(!kbowling_session_create(&s,1,roles,1,123,tables));
    assert(!kbowling_turn_begin(&t,&s,kisaku_bowling_user_throw));
    while(t.phase==KB_TURN_RACK)assert(kbowling_turn_tick(&t,&s,20)==1);
    assert(t.phase==KB_TURN_AIM&&!s.world.active&&!s.awaiting_score);
    assert(!kbowling_turn_pointer(&t,&s,320,440,1));
    assert(!kbowling_turn_pointer(&t,&s,320,430,0));assert(t.phase==KB_TURN_AIM&&!s.awaiting_score);
    assert(!kbowling_turn_pointer(&t,&s,320,440,1));
    for(int y=420;y>=200;y-=20){assert(kbowling_turn_tick(&t,&s,20)==1);assert(!kbowling_turn_pointer(&t,&s,320,y,1));}
    assert(!kbowling_turn_pointer(&t,&s,320,200,0)&&t.phase==KB_TURN_APPROACH);
    unsigned n=0;
    while(t.phase!=KB_TURN_DONE){
        unsigned prior=t.phase;assert(kbowling_turn_tick(&t,&s,20)>=0);assert(++n<10000);
        if(prior==KB_TURN_APPROACH)assert(!s.world.active&&!s.awaiting_score);
    }
    assert(t.throw_sound&&s.awaiting_score&&!s.world.active&&s.world.ball_elapsed>0);
    /* Starting another turn cannot discard an unrecorded score. */
    assert(kbowling_turn_begin(&t,&s,kisaku_bowling_user_throw)==-1);
    while(s.match.player==0){
        assert(kbowling_session_score(&s)>=0);
        if(s.match.player==1)break;
        assert(!kbowling_turn_begin(&t,&s,kisaku_bowling_user_throw));
        assert(!kbowling_turn_pointer(&t,&s,320,440,1));assert(!kbowling_turn_pointer(&t,&s,320,200,1));assert(!kbowling_turn_pointer(&t,&s,320,200,0));
        n=0;while(kbowling_turn_tick(&t,&s,20))assert(++n<10000);
    }
    assert(!kbowling_turn_begin(&t,&s,kisaku_bowling_user_throw));
    n=0;while(kbowling_turn_tick(&t,&s,20))assert(++n<10000);
    assert(s.match.player==1&&s.awaiting_score&&t.cpu&&t.phase==KB_TURN_DONE);
    puts("bowling-turn: mouse gesture, rejected short drag, native approach/release gate, physical completion and CPU turn passed");
}

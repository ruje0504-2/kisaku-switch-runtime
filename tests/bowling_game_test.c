#include "bowling_game.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    KBowlingTables tables={kisaku_bowling_pin_positions,kisaku_bowling_aim,kisaku_bowling_lanes,kisaku_bowling_throws,kisaku_bowling_profiles,kisaku_bowling_spread};
    for(int mode=0;mode<2;mode++)for(int role=0;role<9;role++){
        int32_t roles[4]={role,1,2,3};KBowlingGame g;
        assert(!kbowling_game_begin(&g,mode,roles,1,123,tables,kisaku_bowling_actions,kisaku_bowling_user_throw));
        assert(!kbowling_game_confirm(&g));
        for(unsigned half=0;half<2;half++){
            unsigned ticks=0,throws=0;
            while(g.phase!=KB_GAME_RESULTS){
                if(g.phase==KB_GAME_TURN&&g.turn.phase==KB_TURN_AIM){
                    assert(!kbowling_turn_pointer(&g.turn,&g.session,320,440,1));
                    assert(!kbowling_turn_pointer(&g.turn,&g.session,320,200,1));
                    assert(!kbowling_turn_pointer(&g.turn,&g.session,320,200,0));throws++;
                }
                /* AX rendering is tested separately; emulate its completion
                   acknowledgement, never synthesize fallen pins or scores. */
                if(g.ax_request>=0){g.ax_request=-1;g.ax_wait=0;}
                assert(!kbowling_game_tick(&g,20));assert(++ticks<100000);
            }
            assert(throws>=5&&g.session.match.frame==(half?10u:5u));
            assert(!kbowling_game_confirm(&g)&&g.phase==KB_GAME_RETURN);
            if(!half)assert(!kbowling_game_second_half(&g));
        }
        assert(g.session.match.finished&&g.session.match.result>=0);
        assert(kbowling_game_second_half(&g)==-1);
    }
    puts("bowling-game: 18 full physical matches, two modal halves, all CPU roles, reactions and sweeps: PASS");
}

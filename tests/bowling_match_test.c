#include "bowling_match.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    const int32_t characters[]={0,1,2,3};
    KBowlingMatch m={0};uint32_t rng=1;
    assert(!kbowling_match_create(&m,0,characters));
    for(unsigned i=0;i<5;i++)assert(kbowling_match_throw(&m,10,0,0,kisaku_bowling_profiles,kisaku_bowling_spread,&rng)==(i<4));
    assert(m.first_finished&&!m.active&&!m.finished&&m.result==-1&&m.frame==5&&m.scores[0].total==UINT16_MAX);
    KBowlingMatch before=m;uint32_t old=rng;
    assert(kbowling_match_throw(&m,10,0,0,kisaku_bowling_profiles,kisaku_bowling_spread,&rng)<0);
    assert(!memcmp(&m,&before,sizeof(m))&&rng==old);
    assert(!kbowling_match_second_half(&m));
    for(unsigned i=0;i<7;i++)assert(kbowling_match_throw(&m,10,0,0,kisaku_bowling_profiles,kisaku_bowling_spread,&rng)==(i<6));
    assert(m.finished&&m.result==2&&m.scores[0].total==300&&kbowling_match_second_half(&m)<0);
    assert(!kbowling_match_create(&m,1,characters));
    for(unsigned half=0;half<2;half++){
        if(half)assert(!kbowling_match_second_half(&m));
        unsigned throws=0;
        while(m.active){
            unsigned actor=m.player;assert(actor<2);
            /* User open 3+4; physically simulated opponent all gutter balls. */
            unsigned pins=actor?0:m.ball?4:3;
            int result=kbowling_match_throw(&m,pins,0,!pins,kisaku_bowling_profiles,kisaku_bowling_spread,&rng);
            assert(result>=0&&++throws<=20);
        }
        assert(throws==20);
    }
    assert(m.finished&&m.scores[0].total==70&&m.scores[1].total==0);
    before=m;int32_t invalid[]={0,1,2,9};assert(kbowling_match_create(&m,0,invalid)<0&&!memcmp(&m,&before,sizeof(m)));
    assert(!kbowling_match_create(&m,0,characters));
    assert(kbowling_match_throw(&m,7,0,0,kisaku_bowling_profiles,kisaku_bowling_spread,&rng)==1&&m.rack_remaining==3);
    before=m;old=rng;assert(kbowling_match_throw(&m,4,0,0,kisaku_bowling_profiles,kisaku_bowling_spread,&rng)<0);
    assert(!memcmp(&m,&before,sizeof(m))&&rng==old);
    puts("CBowling match: five-frame suspension, second-half result, physical opponent mode, rack accounting and atomic failure: PASS");
    return 0;
}

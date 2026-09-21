#include "bowling_player.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    uint32_t rng=1;assert(kbowling_rand(&rng)==41&&kbowling_rand(&rng)==18467&&kbowling_rand(&rng)==6334);
    KBowlingScore s;
    uint8_t profile[21];memset(profile,12,sizeof(profile));profile[20]=7;
    kbowling_score_reset(&s);
    for(unsigned frame=0;frame<10;frame++)assert(!kbowling_cpu_frame(&s,frame,0,profile,1,&rng));
    assert(s.total==300&&kbowling_score_pins(&s,9,2)==10); /* 4cb4aa, sampled 5 but writes first=10. */
    for(unsigned role=0;role<9;role++)for(unsigned seed=0;seed<100;seed++){
        rng=seed;kbowling_score_reset(&s);
        for(unsigned frame=0;frame<10;frame++)assert(!kbowling_cpu_frame(&s,frame,role,kisaku_bowling_profiles[role],kisaku_bowling_spread[role],&rng));
        assert(s.total<=300);
        if(role==6)assert(s.total<=90&&kbowling_score_pins(&s,9,2)==15);
    }
    KBowlingScore before=s;uint32_t old=rng;
    assert(kbowling_cpu_frame(&s,10,0,profile,1,&rng)<0&&rng==old&&!memcmp(&s,&before,sizeof(s)));
    KBowlingScore scores[5];for(unsigned i=0;i<5;i++){kbowling_score_reset(&scores[i]);scores[i].total=100;}
    assert(kbowling_result(scores)==1);scores[1].total=101;assert(kbowling_result(scores)==0);
    scores[0].total=300;assert(kbowling_result(scores)==2);scores[2].total=UINT16_MAX;assert(kbowling_result(scores)<0);
    const KBowlingAction commands[]={{8,125},{-2,65535},{9,0},{-3,0},{-1,0}};
    KBowlingActionPlayer p;kbowling_action_begin(&p,(KBowlingActionStream){commands,5},100);
    assert(kbowling_action_tick(&p,99)==0&&p.at==0);
    assert(kbowling_action_tick(&p,100)==1&&p.sprite==8&&p.at==1&&p.due==225);
    assert(kbowling_action_tick(&p,224)==0&&p.at==1);
    assert(kbowling_action_tick(&p,300)==0&&p.at==2&&p.due==65835);
    assert(kbowling_action_tick(&p,65835)==1&&p.sprite==9&&p.at==3);
    assert(kbowling_action_tick(&p,65835)==0&&p.released&&p.at==3&&p.active);
    assert(kbowling_action_tick(&p,70000)==0&&p.at==3); /* Release waits for the caller to change actions. */
    for(unsigned i=0;i<30;i++){
        kbowling_action_begin(&p,kisaku_bowling_actions[i],0);
        for(unsigned tick=0;p.active&&!p.released;tick++){
            assert(tick<=p.stream.count);
            assert(kbowling_action_tick(&p,p.due)>=0);
        }
        assert(!p.active||p.released);
    }
    const KBowlingAction bad[]={{-4,0}};
    kbowling_action_begin(&p,(KBowlingActionStream){bad,1},0);assert(kbowling_action_tick(&p,0)<0&&p.at==0);
    kbowling_action_begin(&p,(KBowlingActionStream){NULL,0},0);assert(!kbowling_action_tick(&p,0)&&!p.active);
    puts("CBowling players: native RNG, nine CPU profiles, last-ball quirk, ties, 30 action streams and unsigned delay: PASS");
    return 0;
}

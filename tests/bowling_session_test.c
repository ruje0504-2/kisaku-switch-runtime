#include "bowling_session.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    KBowlingTables tables={kisaku_bowling_pin_positions,kisaku_bowling_aim,kisaku_bowling_lanes,
        kisaku_bowling_throws,kisaku_bowling_profiles,kisaku_bowling_spread};
    KBowlingWorld layout={0};layout.fallen[0]=1;
    assert(!kbowling_world_split(&layout));
    for(unsigned i=0;i<10;i++)layout.fallen[i]=1;
    layout.fallen[6]=layout.fallen[9]=0;assert(kbowling_world_split(&layout));
    layout.fallen[9]=1;assert(!kbowling_world_split(&layout));
    layout.fallen[0]=0;layout.fallen[9]=0;assert(!kbowling_world_split(&layout));
    /* 4d4970: a fresh second/third rack in frame ten can also be split. */
    KBowlingSession bonus;int32_t bonus_roles[4]={0,1,2,3};
    assert(!kbowling_session_create(&bonus,0,bonus_roles,1,1,tables));
    bonus.match.frame=9;bonus.match.half=1;bonus.match.ball=1;
    assert(!kbowling_score_set(&bonus.match.scores[0],9,0,10,0,0));
    bonus.awaiting_score=1;
    for(unsigned i=0;i<10;i++)bonus.world.fallen[i]=i!=6&&i!=9;
    assert(kbowling_session_score(&bonus)==1);
    assert(bonus.match.scores[0].rolls[9][1]&16);
    assert(bonus.same_rack&&bonus.match.rack_remaining==2);
    unsigned throws=0;
    for(unsigned mode=0;mode<2;mode++)for(unsigned role=0;role<9;role++){
        int32_t roles[4]={(int32_t)role,1,2,3};KBowlingSession s;
        assert(!kbowling_session_create(&s,mode,roles,1,123,tables));
        assert(kbowling_session_second_half(&s)==-1);
        for(unsigned half=0;half<2;half++){
            unsigned guard=0;
            while(s.match.active){
                assert(guard++<32);
                if(s.match.player){
                    KBowlingCPUThrow plan;uint32_t before=s.rng,next;
                    assert(!kbowling_session_cpu_plan(&s,&plan,&next)&&s.rng==before);
                    assert(!kbowling_session_cpu_throw(&s,&plan)&&s.rng==next);
                }else{
                    KBowlingVector controls[3]={{0,0,0},{0,0,23.288f*.5f},{0,0,23.288f}};
                    assert(!kbowling_session_throw(&s,controls,8));
                }
                assert(kbowling_session_score(&s)==-1);
                assert(kbowling_session_throw(&s,s.world.controls,8)==-1);
                unsigned ticks=0;while(kbowling_world_tick(&s.world,10)&&ticks<10000)ticks++;
                assert(ticks<10000);
                assert(kbowling_session_score(&s)>=0);throws++;
                unsigned count=0;for(unsigned i=0;i<10;i++)count+=s.world.present[i];
                if(s.match.active)assert(count==s.match.rack_remaining);
            }
            if(!half){
                assert(s.match.first_finished&&!s.match.finished&&s.match.frame==5);
                KBowlingScore saved[5];memcpy(saved,s.match.scores,sizeof(saved));
                assert(!kbowling_session_second_half(&s));
                assert(!memcmp(saved,s.match.scores,sizeof(saved)));
            }
        }
        assert(s.match.finished&&s.match.frame==10&&s.match.result>=0&&s.match.result<=2);
        assert(kbowling_session_second_half(&s)==-1);
    }
    printf("bowling-session: split layouts, 18 complete simulated matches, %u physical throws passed\n",throws);
    return 0;
}

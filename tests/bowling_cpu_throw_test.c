#include "bowling_cpu_throw.h"
#include "bowling_world.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
static int plan(KBowlingCPUThrow *p,unsigned c,unsigned f,unsigned same,const unsigned pins[10],uint32_t *rng){
    return kbowling_cpu_throw(p,c,f,same,pins,kisaku_bowling_aim,kisaku_bowling_lanes,kisaku_bowling_throws,rng);
}
int main(void){
    unsigned pins[10]={1,1,1,1,1,1,1,1,1,1};
    assert(kbowling_cpu_aim(pins)==0);
    memset(pins,0,sizeof(pins));pins[9]=1;assert(kbowling_cpu_aim(pins)==5);
    pins[6]=1;assert(kbowling_cpu_aim(pins)==3); /* tied groups choose first */
    KBowlingCPUThrow p;
    for(unsigned c=0;c<9;c++)for(unsigned f=0;f<10;f++)for(unsigned same=0;same<2;same++){
        uint32_t rng=123,expected=rng;
        unsigned draws=same?((c==0||c==2||c==3||c==4||c==5||c==6)?1:0):c==6?2:1;
        for(unsigned i=0;i<draws;i++)kbowling_rand(&expected);
        assert(plan(&p,c,f,same,pins,&rng)==0&&rng==expected);
        assert(p.controls[0].z==0&&p.controls[1].z==23.288f*.5f&&p.controls[2].z==23.288f);
        if(same&&(c==1||c==2||c>=5))for(unsigned i=0;i<3;i++)assert(p.controls[i].x==kisaku_bowling_aim[3][i]);
        KBowlingActionPlayer actor;kbowling_action_begin(&actor,p.release,0);
        for(unsigned t=0;t<10000&&!actor.released;t+=20)assert(kbowling_action_tick(&actor,t)>=0);
        assert(actor.released);
        KBowlingWorld world;
        assert(kbowling_world_rack(&world,kisaku_bowling_pin_positions,2,0)==0);
        assert(kbowling_world_throw(&world,p.controls,p.speed)==0);
        unsigned ticks=0;while(kbowling_world_tick(&world,10)&&ticks<10000)ticks++;
        assert(ticks<10000&&kbowling_world_count(&world)<=10);
    }
    uint32_t rng=1,expected=1;kbowling_rand(&expected);
    assert(plan(&p,0,0,0,pins,&rng)==0);
    assert(p.speed==6&&p.approach_frames==7&&p.approach_repeats==6);
    assert(p.controls[1].x==(float)(kisaku_bowling_lanes[0][0][1]+(double)(float)(41*(double).001f-(double).05f)*.5));
    KBowlingCPUThrow before=p;uint32_t saved=rng;
    assert(plan(&p,9,0,0,pins,&rng)==-1&&!memcmp(&before,&p,sizeof(p))&&rng==saved);
    puts("bowling-cpu-throw: nine characters, RNG consumption, aim ties, release and 180 trajectories passed");
    return 0;
}

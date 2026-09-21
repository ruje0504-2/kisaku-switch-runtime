#include "bowling_player.h"
#include "bowling_world.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    KBowlingWorld w;assert(!kbowling_world_rack(&w,kisaku_bowling_pin_positions,2,1));
    KBowlingVector controls[3]={{0,0,0},{0,0,11.644f},{0,0,23.288f}};
    assert(!kbowling_world_throw(&w,controls,8.33));
    assert(kbowling_world_tick(&w,9)&&w.ball_elapsed==0);
    assert(kbowling_world_tick(&w,1)&&w.ball_elapsed==10);
    assert(kbowling_world_tick(&w,200)&&w.ball_elapsed==20&&w.clock==0);
    unsigned ticks=0;while(kbowling_world_tick(&w,10))assert(++ticks<10000);
    unsigned hit=kbowling_world_count(&w);assert(hit>0&&hit<=10&&w.ball_contacts&&!w.active);
    kbowling_world_clear_fallen(&w);assert(!kbowling_world_count(&w));
    unsigned remain=0;for(unsigned i=0;i<10;i++)remain+=w.present[i];assert(remain==10-hit);
    assert(!kbowling_world_throw(&w,controls,8.33));ticks=0;while(kbowling_world_tick(&w,10))assert(++ticks<10000);
    assert(kbowling_world_count(&w)<=remain);
    for(unsigned level=0;level<3;level++)for(int side=-1;side<=1;side+=2){
        assert(!kbowling_world_rack(&w,kisaku_bowling_pin_positions,level,1));
        for(unsigned i=0;i<3;i++)controls[i].x=(float)side;
        assert(!kbowling_world_throw(&w,controls,11.112));ticks=0;
        while(kbowling_world_tick(&w,10))assert(++ticks<10000);
        assert(w.gutter&&!kbowling_world_count(&w));
        for(unsigned i=0;i<10;i++){KBowlingVector p=kbowling_pin_position(&w.pins[i]);assert(isfinite(p.x)&&isfinite(p.z));}
    }
    KBowlingWorld before=w;assert(kbowling_world_throw(&w,controls,NAN)<0&&!memcmp(&w,&before,sizeof(w)));
    printf("CBowling world: native rack, fixed 10ms quantum, center hit=%u, second-ball removal, gutter paths and finite completion: PASS\n",hit);
    return 0;
}

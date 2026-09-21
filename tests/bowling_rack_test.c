#include "bowling_rack.h"
#include "bowling_player.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
static unsigned finish(KBowlingRack *r){unsigned n=0;while(r->phase){assert(kbowling_rack_tick(r,20)>=0);assert(++n<300);}return n;}
int main(void){
    KBowlingWorld w;assert(!kbowling_world_rack(&w,kisaku_bowling_pin_positions,1,1));
    KBowlingRack r;assert(!kbowling_rack_begin(&r,&w,kisaku_bowling_pin_positions,0,0));
    assert(r.machine_visible&&r.pins[0].y==.6f);assert(finish(&r)==60&&!r.machine_visible);
    for(unsigned i=0;i<10;i++)assert(r.visible[i]&&fabsf(r.pins[i].y)<.000001f);
    w.fallen[0]=w.fallen[1]=1;w.pose[0]=1;w.pose[1]=2;
    assert(!kbowling_rack_begin(&r,&w,kisaku_bowling_pin_positions,1,0));
    assert(finish(&r)==195);
    for(unsigned i=0;i<10;i++){assert(r.visible[i]==(i>=2));if(i>=2)assert(r.pins[i].y==0&&r.pins[i].z==kisaku_bowling_pin_positions[i][2]);}
    assert(!kbowling_rack_begin(&r,&w,kisaku_bowling_pin_positions,1,1));assert(finish(&r)==75);
    for(unsigned i=0;i<10;i++)assert(!r.visible[i]);
    w.active=1;KBowlingRack saved=r;assert(kbowling_rack_begin(&r,&w,kisaku_bowling_pin_positions,1,0)==-1&&!memcmp(&r,&saved,sizeof(r)));
    assert(kbowling_pin_pose((KBowlingVector){0,0,1},10)==1);
    assert(kbowling_pin_pose((KBowlingVector){0,0,1},11)==2);
    assert(kbowling_pin_pose((KBowlingVector){0,0,-1},10)==2);
    assert(kbowling_pin_pose((KBowlingVector){-1,0,0},10)==3);
    assert(kbowling_pin_pose((KBowlingVector){1,0,0},10)==4);
    puts("bowling-rack: native lowering, survivor preservation, full sweep, fall direction and separate clocks passed");
}

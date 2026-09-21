#include "bowling_projection.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    KBowlingCamera c=kbowling_camera();int x,y,xb,yb;
    assert(!kbowling_project(&c,(KBowlingVector){0,1,18},0,&x,&y)&&x==188&&y==116);
    assert(!kbowling_project(&c,(KBowlingVector){0,0,0},0,&x,&y));
    assert(x==188&&y>300&&y<310);
    assert(!kbowling_project(&c,(KBowlingVector){0,0,0},1,&xb,&yb)&&xb==x-4&&yb==y-8);
    int left,right;
    assert(!kbowling_project(&c,(KBowlingVector){-.65f,0,18},0,&left,&y));
    assert(!kbowling_project(&c,(KBowlingVector){.65f,0,18},0,&right,&y));
    assert(left<188&&right>188&&left+right==376);
    assert(kbowling_project(&c,(KBowlingVector){0,0,-100},0,&x,&y)==-1);
    assert(kbowling_project(&c,(KBowlingVector){NAN,0,0},0,&x,&y)==-1);
    kbowling_ball_tile(0,&x,&y);assert(x==0&&y==0);
    kbowling_ball_tile(6.096000075340271,&x,&y);assert(x==0&&y==32);
    kbowling_ball_tile(23,&x,&y);assert(x==0&&y==96);
    kbowling_pin_tile(18,0,900,&x,&y);assert(x==0&&y==0);
    kbowling_pin_tile(18,1,100,&x,&y);assert(x==24&&y==48);
    kbowling_pin_tile(18.6,2,200,&x,&y);assert(x==48&&y==168);
    kbowling_pin_tile(19,3,300,&x,&y);assert(x==72&&y==216);
    kbowling_pin_tile(20,4,999,&x,&y);assert(x==168&&y==192);
    puts("bowling-projection: native camera, distinct anchors, depth tiers, ball atlas and invalid coordinates passed");
}

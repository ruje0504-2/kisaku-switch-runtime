#include "bowling_throw.h"
#include <assert.h>
#include <stdio.h>
static int near(double a,double b){return fabs(a-b)<0.00001;}
int main(void){
    KBowlingThrow t;
    assert(near(kbowling_throw_power(0,0),11.112)&&near(kbowling_throw_power(400,0),11.112));
    assert(near(kbowling_throw_power(6400,0),6.945)&&near(kbowling_throw_power(12400,0),2.778));
    assert(near(kbowling_throw_power(2700,1),6.945)&&near(kbowling_throw_power(1100,2),6.945));
    kbowling_throw_press(&t,320,400,100);kbowling_throw_drag(&t,320,281,200,0);
    assert(!t.crossed&&t.distance==119&&!kbowling_throw_release(&t)&&!t.ready);
    kbowling_throw_press(&t,320,400,100);kbowling_throw_drag(&t,320,280,200,0);
    assert(t.crossed&&t.mid_x==90);
    kbowling_throw_drag(&t,320,160,300,0);assert(t.distance==240&&kbowling_throw_release(&t));
    assert(t.ready&&t.start_x==90&&t.mid_x==90&&t.end_x==90);
    assert(t.controls[0].x==0&&t.controls[1].x==0&&t.controls[2].x==0);
    assert(near(t.controls[1].z,11.644)&&near(t.controls[2].z,23.288));
    double w[3];kbowling_spline_weights(0,w);assert(w[0]==.5&&w[1]==.5&&w[2]==0);
    kbowling_spline_weights(.5,w);assert(w[0]==.125&&w[1]==.75&&w[2]==.125);
    kbowling_spline_weights(1,w);assert(w[0]==0&&w[1]==.5&&w[2]==.5);
    unsigned moving=1;KBowlingVector v=kbowling_ball_position(t.controls,t.speed,0,&moving);assert(v.x==0&&v.z==0&&moving);
    float z=0;
    for(unsigned ms=10;ms<2200;ms+=10){v=kbowling_ball_position(t.controls,t.speed,ms,&moving);assert(v.x==0&&v.z>=z);z=v.z;}
    assert(!moving&&near(z,21.8325)); /* Last spline segment retains the native endpoint weighting. */
    KBowlingVector gutter[3]={{1,0,0},{1,0,11.644f},{1,0,23.288f}};
    v=kbowling_ball_position(gutter,8.33,1000,NULL);assert(near(v.x,.76));
    for(unsigned i=0;i<3;i++)gutter[i].x=-1;
    v=kbowling_ball_position(gutter,8.33,1000,NULL);assert(near(v.x,-.76));
    /* Downward movement does not increase power/gesture distance. */
    kbowling_throw_press(&t,320,200,0);kbowling_throw_drag(&t,640,300,10,0);assert(t.distance==0&&t.lane_x==90);
    kbowling_throw_drag(&t,640,180,20,0);assert(t.distance==120&&t.lane_x==180&&t.mid_x==180);
    puts("CBowling throw: drag threshold, input lag, three power curves, spline weights and gutter clamp: PASS");
    return 0;
}

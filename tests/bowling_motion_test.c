#include "bowling_motion.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    KBowlingPinMotion p={.origin={1,0,20},.direction={1,0,0},.speed=10};
    KBowlingVector a=kbowling_pin_position(&p);assert(a.x==1&&a.z==20&&kbowling_pin_moving(&p));
    p.elapsed=1000;a=kbowling_pin_position(&p);assert(fabs(a.x-1.69314718)<0.000001&&a.z==20);
    KBowlingVector v=kbowling_pin_velocity(&p);assert(v.x>.499&&v.x<.502&&v.y==0&&v.z==0&&p.elapsed==1000);
    p.elapsed=100000;assert(!kbowling_pin_moving(&p)&&p.elapsed==100000);
    kbowling_pin_redirect(&p,a,(KBowlingVector){0,0,-1},5);assert(!p.elapsed&&p.speed==5);
    p.elapsed=1000;a=kbowling_pin_position(&p);assert(fabs(a.z-19.7972674)<0.000002);
    p.direction=(KBowlingVector){0};a=kbowling_pin_position(&p);assert(!kbowling_pin_moving(&p)&&a.x==p.origin.x&&a.z==p.origin.z);
    puts("CBowling pin motion: native logarithmic drag, 1ms velocity, 100ms stop prediction and redirect clock: PASS");
    return 0;
}

#include "bowling_contact.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    KBowlingPinMotion pin={.origin={0,0,20}};
    assert(kbowling_ball_contact(&pin,.121f,(KBowlingVector){0,0,19.9f},(KBowlingVector){0,0,1},.21f,8));
    assert(pin.speed==64&&pin.direction.z==1&&pin.elapsed==0);
    pin=(KBowlingPinMotion){.origin={.06f,0,20.08f}};
    assert(kbowling_ball_contact(&pin,.121f,(KBowlingVector){0,0,20},(KBowlingVector){0,0,1},.21f,10));
    assert(fabs(pin.speed-64)<.002); /* projection .8; squared projected speed, not 8. */
    KBowlingPinMotion before=pin;
    assert(!kbowling_ball_contact(&pin,.121f,(KBowlingVector){10,0,20},(KBowlingVector){0,0,1},.21f,10));
    assert(pin.origin.x==before.origin.x&&pin.speed==before.speed);
    KBowlingPinMotion a={.origin={0,0,20},.direction={1,0,0},.speed=10,.elapsed=1000};
    KBowlingPinMotion b={.origin={.8f,0,20}};
    KBowlingVector velocity=kbowling_pin_velocity(&a);double impulse=kbowling_vector_length(velocity);
    assert(kbowling_pin_contact(&a,.121f,&b,.121f));
    assert(a.direction.x==0&&b.direction.x==1&&a.speed==impulse&&b.speed==impulse&&!a.elapsed&&!b.elapsed);
    b.origin.x=4;assert(!kbowling_pin_contact(&a,.121f,&b,.121f));
    a=(KBowlingPinMotion){.origin={0,0,20},.direction={1,0,0},.speed=10,.elapsed=1000};
    KBowlingVector current=kbowling_pin_position(&a),previous=current;previous.x-=.2f;
    b=(KBowlingPinMotion){.origin={current.x+.3f,0,20}};
    assert(kbowling_pin_pair(&a,.121f,previous,&b,.121f)==1);
    assert(a.elapsed==1000&&a.speed==10&&b.elapsed==0&&b.speed==2.5&&b.direction.x==1);
    b=(KBowlingPinMotion){.origin={current.x+1,0,20}};
    assert(!kbowling_pin_pair(&a,.121f,previous,&b,.121f)&&b.speed==0);

    a=(KBowlingPinMotion){.origin={.5f,0,20},.direction={1,0,0},.speed=10,.elapsed=1000};
    assert(kbowling_pin_boundary(&a,.121f)&&fabs(a.origin.x-.7)<.00001&&a.speed>0&&a.speed<.1&&!a.elapsed);
    a=(KBowlingPinMotion){.origin={0,0,24},.direction={0,0,1},.speed=10};
    assert(kbowling_pin_boundary(&a,.121f)&&a.speed==0&&fabs(a.origin.z-23.288)<.00001);
    a=(KBowlingPinMotion){.origin={0,0,16},.direction={0,0,-1},.speed=10};
    assert(kbowling_pin_boundary(&a,.121f)&&a.speed==0&&fabs(a.origin.z-17.288)<.00001);
    puts("CBowling contacts: squared ball impulse, two-pin separation, unchanged misses and native boundary damping: PASS");
    return 0;
}

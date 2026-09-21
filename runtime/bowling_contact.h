#ifndef KISAKU_BOWLING_CONTACT_H
#define KISAKU_BOWLING_CONTACT_H
#include "bowling_motion.h"
static inline KBowlingVector kbowling_vector_sub(KBowlingVector a,KBowlingVector b){return (KBowlingVector){a.x-b.x,a.y-b.y,a.z-b.z};}
static inline double kbowling_vector_length(KBowlingVector a){return sqrt((double)a.x*a.x+(double)a.y*a.y+(double)a.z*a.z);}
static inline KBowlingVector kbowling_vector_normal(KBowlingVector a){
    float length=(float)kbowling_vector_length(a);
    return length==0?(KBowlingVector){0}:(KBowlingVector){a.x/length,a.y/length,a.z/length};
}
static inline KBowlingVector kbowling_vector_add(KBowlingVector a,KBowlingVector b){return (KBowlingVector){a.x+b.x,a.y+b.y,a.z+b.z};}
/* 4d2882..4d29d2: contact uses X/Z distance; the impulse is the SQUARE of
   ball speed projected onto the outward contact normal. Pseudocode omits
   the extra multiply before the call to 40ce70; the assembly includes it. */
static inline int kbowling_ball_contact(KBowlingPinMotion *pin,float pin_width,
    KBowlingVector ball,KBowlingVector ball_velocity,float ball_width,double ball_speed){
    KBowlingVector position=kbowling_pin_position(pin),delta=kbowling_vector_sub(position,ball);
    double radius=pin_width*0.5+ball_width*0.5;
    if(sqrt((double)delta.x*delta.x+(double)delta.z*delta.z)>radius)return 0;
    KBowlingVector normal=kbowling_vector_normal(delta),direction=kbowling_vector_normal(ball_velocity);
    double projected=ball_speed*((double)direction.x*normal.x+(double)direction.y*normal.y+(double)direction.z*normal.z);
    kbowling_pin_redirect(pin,position,normal,projected*projected);
    return 1;
}
/* 4d1bxx..4d20xx overlap branch: both impulses use the sum of speeds
   sampled before either body changes. Each new direction adds a separating
   normal to that body's unit velocity and normalizes again. */
static inline int kbowling_pin_contact(KBowlingPinMotion *a,float width_a,KBowlingPinMotion *b,float width_b){
    KBowlingVector pa=kbowling_pin_position(a),pb=kbowling_pin_position(b),delta=kbowling_vector_sub(pa,pb);
    if(sqrt((double)delta.x*delta.x+(double)delta.z*delta.z)>width_a*0.5+width_b*0.5+0.125)return 0;
    KBowlingVector va=kbowling_pin_velocity(a),vb=kbowling_pin_velocity(b);
    double impulse=kbowling_vector_length(va)+kbowling_vector_length(vb);
    KBowlingVector na=kbowling_vector_normal(delta),nb={-na.x,-na.y,-na.z};
    KBowlingVector da=kbowling_vector_normal(kbowling_vector_add(kbowling_vector_normal(va),na));
    KBowlingVector db=kbowling_vector_normal(kbowling_vector_add(kbowling_vector_normal(vb),nb));
    kbowling_pin_redirect(a,pa,da,impulse);kbowling_pin_redirect(b,pb,db,impulse);
    return 1;
}
/* 4d169c..4d1b1d fast-motion branch, evaluated before the overlap branch.
   This is the native radial quadratic, including its positive B term; do
   not replace it with a conventional swept-sphere solver. */
static inline int kbowling_pin_pair(KBowlingPinMotion *a,float width_a,KBowlingVector previous_a,
    KBowlingPinMotion *b,float width_b){
    KBowlingVector pa=kbowling_pin_position(a),pb=kbowling_pin_position(b);
    KBowlingVector travelled=kbowling_vector_sub(pa,previous_a);
    int hit=0;
    if(sqrt((double)travelled.x*travelled.x+(double)travelled.z*travelled.z)>width_a&&a->speed!=0){
        double speed=a->speed,distance=kbowling_vector_length(kbowling_vector_sub(pa,pb));
        double radius=(float)(width_a+width_b)+0.125;
        double aa=speed*speed,bb=2*speed*distance,cc=distance*distance-radius*radius;
        double center=bb/aa/2,discriminant=center*center-cc/aa;
        if(discriminant>=0){
            double root=sqrt(discriminant),lo=-center-root,hi=-center+root;
            if(hi>=0){
                float time=(float)(lo<0?hi:lo),magnitude=(float)speed;
                KBowlingVector direction=kbowling_vector_normal(kbowling_pin_velocity(a));
                direction=(KBowlingVector){(float)(direction.x*magnitude)*time,(float)(direction.y*magnitude)*time,(float)(direction.z*magnitude)*time};
                KBowlingVector contact=kbowling_vector_add(pa,direction);
                KBowlingVector normal=kbowling_vector_normal(kbowling_vector_sub(pb,contact));
                KBowlingVector outgoing=kbowling_vector_normal(kbowling_vector_add(kbowling_vector_normal(kbowling_pin_velocity(b)),normal));
                kbowling_pin_redirect(b,pb,outgoing,speed*0.25);hit=1;
            }
        }
    }
    if(kbowling_pin_contact(a,width_a,b,width_b))hit|=2;
    return hit;
}
/* 4d21xx..4d25xx: side boards add a unit inward normal, clamp center to
   +/-0.7 and retain 15% of speed. End boundaries stop at native Z limits. */
static inline int kbowling_pin_boundary(KBowlingPinMotion *p,float width){
    KBowlingVector position=kbowling_pin_position(p);
    int side=position.x-width*0.5< -0.800000011920929?1:position.x+width*0.5>0.800000011920929?-1:0;
    int changed=0;
    if(side){
        KBowlingVector v=kbowling_pin_velocity(p),direction=kbowling_vector_normal(v);
        direction.x+=(float)side;direction=kbowling_vector_normal(direction);
        position.x=side>0?-0.7f:0.7f;
        kbowling_pin_redirect(p,position,direction,kbowling_vector_length(v)*0.15);changed=1;
    }
    if(position.z>=23.288000226020813||position.z<=17.288000226020813){
        position.z=position.z>=23.288000226020813?(float)23.288000226020813:(float)17.288000226020813;
        kbowling_pin_redirect(p,position,(KBowlingVector){0},0);changed=1;
    }
    return changed;
}
#endif

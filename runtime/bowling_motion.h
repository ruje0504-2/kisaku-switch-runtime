#ifndef KISAKU_BOWLING_MOTION_H
#define KISAKU_BOWLING_MOTION_H
#include "bowling_throw.h"
typedef struct {
    KBowlingVector origin,direction;
    double speed;
    uint32_t elapsed;
} KBowlingPinMotion;
/* 4cd540 / 4358b0 (fldln2 + fyl2x): logarithmic displacement. Preserve
   the native float stores between each vector multiplication. */
static inline KBowlingVector kbowling_pin_position(const KBowlingPinMotion *p){
    KBowlingVector result=p->origin;
    if(!p->elapsed||(p->direction.x==0&&p->direction.y==0&&p->direction.z==0))return result;
    float distance=(float)log(p->elapsed/1000.0*p->speed*0.10000000149011612+1.0);
    float speed=(float)p->speed;
    float dx=(float)((float)(p->direction.x*distance)*speed)*0.1f;
    float dy=(float)((float)(p->direction.y*distance)*speed)*0.1f;
    float dz=(float)((float)(p->direction.z*distance)*speed)*0.1f;
    result.x+=dx;result.y+=dy;result.z+=dz;
    return result;
}
/* 4cd970: velocity is a 1ms backward difference, multiplied by 1000. */
static inline KBowlingVector kbowling_pin_velocity(const KBowlingPinMotion *p){
    if(!p->elapsed)return (KBowlingVector){0};
    KBowlingPinMotion previous=*p;previous.elapsed--;
    KBowlingVector a=kbowling_pin_position(p),b=kbowling_pin_position(&previous);
    return (KBowlingVector){(a.x-b.x)*1000,(a.y-b.y)*1000,(a.z-b.z)*1000};
}
/* 4cd890: continue simulating when the next 100ms displacement exceeds
   0.05. The prediction must not advance the actual object clock. */
static inline int kbowling_pin_moving(const KBowlingPinMotion *p){
    if(p->direction.x==0&&p->direction.y==0&&p->direction.z==0)return 0;
    KBowlingPinMotion future=*p;future.elapsed+=100;
    KBowlingVector a=kbowling_pin_position(p),b=kbowling_pin_position(&future);
    float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
    return sqrt((double)dx*dx+(double)dy*dy+(double)dz*dz)>0.05;
}
static inline void kbowling_pin_redirect(KBowlingPinMotion *p,KBowlingVector origin,KBowlingVector direction,double speed){
    p->origin=origin;p->direction=direction;p->speed=speed;p->elapsed=0;
}
#endif

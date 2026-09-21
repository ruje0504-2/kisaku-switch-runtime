#ifndef KISAKU_BOWLING_THROW_H
#define KISAKU_BOWLING_THROW_H
#include <stdint.h>
#include <math.h>
typedef struct {float x,y,z;} KBowlingVector;
typedef struct {
    int last_x,last_y,start_x,mid_x,end_x,lane_x,distance,crossed;
    unsigned held,ready;
    uint32_t started;
    double speed;
    KBowlingVector controls[3];
} KBowlingThrow;
static inline int kbowling_clamp(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
static inline double kbowling_throw_power(uint32_t elapsed,unsigned difficulty){
    static const double threshold[]={400,200,100},duration[]={12000,5000,2000};
    unsigned level=difficulty<2?difficulty:2;
    double t=((double)elapsed-threshold[level])/duration[level];
    if(t<0)t=0;
    if(t>1)t=1;
    return 11.112-(11.112-2.778)*t;
}
/* 4ca850: logical 640x480 mouse coordinates map to a 180x240 guide.
   Horizontal input is sampled from the previous poll; only upward movement
   contributes. Crossing 120 fixes the middle control point. */
static inline void kbowling_throw_press(KBowlingThrow *t,int x,int y,uint32_t now){
    *t=(KBowlingThrow){.last_x=x,.last_y=y,.start_x=kbowling_clamp(x,0,640)*180/640,.held=1,.started=now,.speed=1};
    t->lane_x=t->start_x;
}
static inline void kbowling_throw_drag(KBowlingThrow *t,int x,int y,uint32_t now,unsigned difficulty){
    if(!t->held||t->ready)return;
    int guide=kbowling_clamp(t->last_x,0,640)*180/640;
    int64_t dy=(int64_t)t->last_y-y;
    int rise=dy<0?0:dy>480?480:(int)dy;
    t->last_x=x;t->last_y=y;t->lane_x=guide;
    t->distance=kbowling_clamp(t->distance+rise,0,240);
    if(!t->crossed&&t->distance>119){
        t->crossed=1;
        /* start guide y=239, hence the native denominator distance-1. */
        t->mid_x=t->start_x+(int)((double)(guide-t->start_x)*120/(t->distance-1));
    }
    t->speed=kbowling_throw_power(now-t->started,difficulty);
}
static inline int kbowling_throw_release(KBowlingThrow *t){
    if(!t->held)return 0;
    t->held=0;
    if(!t->crossed)return 0;
    int delta=t->distance-121;if(delta<1)delta=1;
    float slope=(float)(t->lane_x-t->mid_x)/(float)delta;
    t->end_x=t->mid_x+(int)((double)slope*120);
    /* 5d4b30 rectangle uses float +/-0.65; 543108 is a FLOAT 23.288. */
    double width=(double)0.6499999761581421f-(-0.6499999761581421f);
    const float length=23.288f;
    t->controls[0]=(KBowlingVector){(float)(width*(t->start_x/180.0-0.5)),0,0};
    t->controls[1]=(KBowlingVector){(float)(width*(t->mid_x/180.0-0.5)),0,length*0.5f};
    t->controls[2]=(KBowlingVector){(float)(width*(t->end_x/180.0-0.5)),0,length};
    t->ready=1;return 1;
}
/* 4ce240 uniform quadratic B-spline weights (not Bezier weights). */
static inline void kbowling_spline_weights(double t,double w[3]){
    w[0]=(1-t)*(1-t)*0.5;w[1]=(1-t)*t+0.5;w[2]=t*t*0.5;
}
/* 4ce3b0 clamps out-of-lane x to +/-0.76, preserving the gutter path. */
static inline KBowlingVector kbowling_ball_position(const KBowlingVector controls[3],double speed,uint32_t elapsed,unsigned *moving){
    KBowlingVector out=controls[0];
    if(!elapsed)return out;
    double span=23.288000226020813-out.z;
    double distance=elapsed/1000.0*speed+out.z;
    if(distance>span){distance=span;if(moving)*moving=0;}
    double t=distance/span*2,w[3],x,z;
    if(t<0.5){
        kbowling_spline_weights(t+0.5,w);
        x=controls[0].x*w[0]+controls[0].x*w[1]+controls[1].x*w[2];
        z=(controls[0].z-span*0.5)*w[0]+controls[0].z*w[1]+controls[1].z*w[2];
    }else if(t<1.5){
        kbowling_spline_weights(t-0.5,w);
        x=controls[0].x*w[0]+controls[1].x*w[1]+controls[2].x*w[2];
        z=controls[0].z*w[0]+controls[1].z*w[1]+controls[2].z*w[2];
    }else{
        kbowling_spline_weights(t-1.5,w);
        x=controls[1].x*w[0]+controls[2].x*w[1]+controls[2].x*w[2];
        z=controls[1].z*w[0]+controls[2].z*w[1]+controls[2].z*w[2];
    }
    if(x>0.65)x=0.76;else if(x< -0.65)x= -0.76;
    return (KBowlingVector){(float)x,0,(float)z};
}
#endif

#ifndef KISAKU_BOWLING_PROJECTION_H
#define KISAKU_BOWLING_PROJECTION_H
#include "bowling_contact.h"
/* 4d5390 -> CCamera: near 1, far 200, fov(float pi/3), eye(0,1.5,-14),
   target(0,1,18). 4f2130 uses a left-handed look-at matrix; 4f2050 uses
   cot(fov/2). Matrices and divided coordinates are stored as float. */
typedef struct {float matrix[16];} KBowlingCamera;
static inline float kbowling_dot(KBowlingVector a,KBowlingVector b){
    return (float)((double)a.x*b.x+(double)a.y*b.y+(double)a.z*b.z);
}
static inline KBowlingVector kbowling_cross(KBowlingVector a,KBowlingVector b){
    return (KBowlingVector){(float)((double)a.y*b.z-(double)a.z*b.y),
        (float)((double)a.z*b.x-(double)a.x*b.z),(float)((double)a.x*b.y-(double)a.y*b.x)};
}
static inline KBowlingCamera kbowling_camera(void){
    KBowlingVector eye={0,1.5f,-14},forward=kbowling_vector_normal((KBowlingVector){0,-.5f,32});
    KBowlingVector up=kbowling_vector_normal(kbowling_vector_sub((KBowlingVector){0,1,0},
        (KBowlingVector){(float)(forward.x*(double)forward.y),(float)(forward.y*(double)forward.y),(float)(forward.z*(double)forward.y)}));
    KBowlingVector right=kbowling_cross(up,forward);
    float view[16]={right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,
        -kbowling_dot(eye,right),-kbowling_dot(eye,up),-kbowling_dot(eye,forward),1};
    float angle=1.0471975803375244f*.5f,cot=(float)(cos((double)angle)/sin((double)angle));
    float projection[16]={cot,0,0,0,0,cot,0,0,0,0,(float)(200.0/199),1,0,0,(float)(-200.0/199),0};
    KBowlingCamera result={0};
    for(unsigned row=0;row<4;row++)for(unsigned col=0;col<4;col++){
        double sum=0;for(unsigned k=0;k<4;k++)sum+=(double)view[row*4+k]*projection[k*4+col];
        result.matrix[row*4+col]=(float)sum;
    }
    return result;
}
/* 5000a0 applies 1200 after perspective divide. 4d0d80/4d0b50 then
   truncate toward zero and add distinct ball/pin anchors. */
static inline int kbowling_project(const KBowlingCamera *camera,KBowlingVector p,int ball,int *x,int *y){
    if(!camera||!x||!y||!isfinite(p.x)||!isfinite(p.y)||!isfinite(p.z))return -1;
    const float *m=camera->matrix;
    float w=(float)((double)p.x*m[3]+(double)p.y*m[7]+(double)p.z*m[11]+m[15]);
    if(!isfinite(w)||w<=0)return -1;
    float px=(float)(((double)p.x*m[0]+(double)p.y*m[4]+(double)p.z*m[8]+m[12])/w);
    float py=(float)(((double)p.x*m[1]+(double)p.y*m[5]+(double)p.z*m[9]+m[13])/w);
    px=(float)((double)px*1200);py=(float)((double)py*1200);
    if(!isfinite(px)||!isfinite(py)||fabsf(px)>1000000||fabsf(py)>1000000)return -1;
    *x=(ball?184:188)+(int)px;*y=(ball?108:116)-(int)py;return 0;
}
static inline void kbowling_pin_tile(double z,unsigned pose,unsigned elapsed,int *x,int *y){
    unsigned step=elapsed/100;if(step>3)step=3;
    *x=pose&&step?(int)step*24:0;
    *y=pose&&step?(pose==1?48:pose==2?72:pose==3?24:0):0;
    if(z>=19.202400237321854){*x+=96;*y+=192;}
    else if(z>=18.745200231671333)*y+=192;
    else if(z>=18.470880228281022)*y+=96;
}
static inline void kbowling_ball_tile(double z,int *x,int *y){
    double frame=z/0.30480000376701355;
    int n=frame<=0?0:frame>=60?60:(int)frame;*x=(n%20)*32;*y=(n/20)*32;
}
#endif

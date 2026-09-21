#ifndef KISAKU_BOWLING_WORLD_H
#define KISAKU_BOWLING_WORLD_H
#include "bowling_contact.h"
#include <string.h>
typedef struct {
    KBowlingPinMotion pins[10];
    KBowlingVector controls[3],ball;
    float pin_width,ball_width;
    double ball_speed;
    uint32_t clock,ball_elapsed;
    unsigned present[10],fallen[10],pose[10],pose_elapsed[10],active,gutter,loud_hit;
    unsigned contacts,ball_contacts,ball_hit_mask;
} KBowlingWorld;
/* 4d1170 chooses the first-hit fall direction; 4cd860 resets a separate
   animation clock, not the motion clock reset by later collisions. */
static inline unsigned kbowling_pin_pose(KBowlingVector direction,double speed){
    double angle=direction.x==0?(direction.z<0?-0x1.921fb54442d18p+0:0x1.921fb54442d18p+0):atan2((double)direction.z,(double)direction.x);
    if(angle>0x1.f6a7a2955385ep+0||angle< -0x1.f6a7a2955385ep+0)return speed<=10?3:4;
    if(angle>0x1.2d97c7f3321d2p+0)return speed<=10?1:2;
    if(angle> -0x1.2d97c7f3321d2p+0)return speed<=10?4:3;
    return speed<=10?2:1;
}
static inline int kbowling_world_rack(KBowlingWorld *w,const float positions[10][3],unsigned difficulty,unsigned user){
    if(!w||!positions)return -1;
    for(unsigned i=0;i<10;i++)for(unsigned c=0;c<3;c++)if(!isfinite(positions[i][c]))return -1;
    KBowlingWorld next={0};
    float scale=!user?1:difficulty==0?1.3f:difficulty==1?1.15f:1;
    next.pin_width=.121f*scale;next.ball_width=.21f*scale;
    for(unsigned i=0;i<10;i++){
        next.present[i]=1;
        next.pins[i].origin=(KBowlingVector){positions[i][0],positions[i][1],positions[i][2]};
    }
    *w=next;return 0;
}
/* 4d3900 removes only fallen pins between balls, clearing their hit flags
   so the next score counts newly fallen pins rather than the whole frame. */
static inline void kbowling_world_clear_fallen(KBowlingWorld *w){
    for(unsigned i=0;i<10;i++)if(w->fallen[i]){w->present[i]=w->fallen[i]=0;w->pins[i].direction=(KBowlingVector){0};w->pins[i].speed=0;w->pins[i].elapsed=0;}
}
static inline unsigned kbowling_world_count(const KBowlingWorld *w){
    unsigned count=0;for(unsigned i=0;i<10;i++)count+=w->fallen[i]!=0;return count;
}
static inline int kbowling_world_throw(KBowlingWorld *w,const KBowlingVector controls[3],double speed){
    if(!w||!controls||w->active||!isfinite(speed)||speed<=0)return -1;
    for(unsigned i=0;i<3;i++)if(!isfinite(controls[i].x)||!isfinite(controls[i].y)||!isfinite(controls[i].z))return -1;
    if(controls[0].z>=23.288000226020813)return -1;
    memcpy(w->controls,controls,sizeof(w->controls));w->ball=controls[0];w->ball_speed=speed;
    w->clock=w->ball_elapsed=w->gutter=w->loud_hit=w->contacts=w->ball_contacts=0;w->active=1;return 0;
}
/* 4d12b0 discards elapsed time beyond the first 10ms quantum. Do not
   silently turn this into an accumulator that catches up multiple steps. */
static inline int kbowling_world_tick(KBowlingWorld *w,uint32_t elapsed){
    if(!w)return 0;
    w->ball_hit_mask=0;if(!w->active)return 0;
    w->clock+=elapsed;if(w->clock<10)return 1;w->clock=0;
    KBowlingVector previous[10];
    for(unsigned i=0;i<10;i++){previous[i]=kbowling_pin_position(&w->pins[i]);w->pins[i].elapsed+=10;if(w->pose[i])w->pose_elapsed[i]+=10;}
    unsigned new_hits=0;
    for(unsigned i=0;i<10;i++)if(w->present[i]){
        for(unsigned j=i+1;j<10;j++)if(w->present[j]){
            int hit=kbowling_pin_pair(&w->pins[i],w->pin_width,previous[i],&w->pins[j],w->pin_width);
            if(hit){
                if((hit&2)&&!w->fallen[i]){w->pose[i]=kbowling_pin_pose(w->pins[i].direction,w->pins[i].speed);w->pose_elapsed[i]=0;}
                if(!w->fallen[j]){w->pose[j]=kbowling_pin_pose(w->pins[j].direction,w->pins[j].speed*((hit&2)?1:4));w->pose_elapsed[j]=0;}
                if(!w->fallen[i]||!w->fallen[j])new_hits+=((hit&1)!=0)+((hit&2)!=0);
                w->fallen[i]=w->fallen[j]=1;w->contacts++;}
        }
        kbowling_pin_boundary(&w->pins[i],w->pin_width);
    }
    w->ball_elapsed+=10;unsigned moving=1;
    w->ball=kbowling_ball_position(w->controls,w->ball_speed,w->ball_elapsed,&moving);
    KBowlingVector old=kbowling_ball_position(w->controls,w->ball_speed,w->ball_elapsed-1,NULL);
    KBowlingVector direction=kbowling_vector_normal(kbowling_vector_sub(w->ball,old));
    if(!kbowling_world_count(w)&&!w->gutter&&(w->ball.x<-.65f||w->ball.x>.65f))w->gutter=1;
    for(unsigned i=0;i<10;i++)if(w->present[i]&&kbowling_ball_contact(&w->pins[i],w->pin_width,w->ball,direction,w->ball_width,w->ball_speed)){
        if(!w->fallen[i]){new_hits++;w->pose[i]=kbowling_pin_pose(w->pins[i].direction,w->pins[i].speed);w->pose_elapsed[i]=0;}
        w->fallen[i]=1;w->ball_contacts++;w->ball_hit_mask|=1u<<i;
    }
    if(new_hits>3&&w->ball_speed>6)w->loud_hit=1;
    if(moving)return 1;
    for(unsigned i=0;i<10;i++)if(kbowling_pin_moving(&w->pins[i]))return 1;
    for(unsigned i=0;i<10;i++)kbowling_pin_redirect(&w->pins[i],kbowling_pin_position(&w->pins[i]),(KBowlingVector){0},0);
    w->active=0;return 0;
}
#endif

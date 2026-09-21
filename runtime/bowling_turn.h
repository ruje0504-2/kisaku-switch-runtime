#ifndef KISAKU_BOWLING_TURN_H
#define KISAKU_BOWLING_TURN_H
#include "bowling_session.h"
#include "bowling_rack.h"
enum {KB_TURN_IDLE,KB_TURN_RACK,KB_TURN_AIM,KB_TURN_APPROACH,KB_TURN_RELEASE,KB_TURN_FLIGHT,KB_TURN_SETTLE,KB_TURN_DONE};
typedef struct {
    KBowlingRack rack;
    KBowlingThrow gesture;
    KBowlingCPUThrow throw_plan;
    KBowlingActionPlayer actor;
    KBowlingVector actor_position;
    KBowlingActionStream user_release;
    unsigned phase,clock,step,now,cpu;
    int pointer_x,pointer_y,sprite;
    unsigned held,throw_sound;
} KBowlingTurn;
static inline int kbowling_turn_setup(KBowlingTurn *t,KBowlingSession *s){
    KBowlingTurn next=*t;
    if(next.cpu){
        uint32_t random;
        if(kbowling_session_cpu_plan(s,&next.throw_plan,&random))return -1;
        s->rng=random;next.phase=KB_TURN_APPROACH;
        next.actor_position=next.throw_plan.controls[0];next.actor_position.x-=.125f;next.actor_position.z-=5;
        next.sprite=0;
    }else{next.phase=KB_TURN_AIM;next.sprite=0;next.actor_position=(KBowlingVector){-.125f,0,-5};}
    next.clock=next.step=0;*t=next;return 0;
}
static inline int kbowling_turn_begin(KBowlingTurn *t,KBowlingSession *s,KBowlingActionStream user_release){
    if(!t||!s||!s->match.active||s->awaiting_score||!user_release.commands||!user_release.count)return -1;
    KBowlingTurn next={.cpu=s->match.player!=0,.user_release=user_release,.pointer_x=320,.pointer_y=440};
    if(s->same_rack){if(kbowling_turn_setup(&next,s))return -1;}
    else{if(kbowling_rack_begin(&next.rack,&s->world,s->tables.positions,0,0))return -1;next.phase=KB_TURN_RACK;}
    *t=next;return 0;
}
/* Poll coordinates and button state together. A short drag is rejected and
   returns to aiming; no fabricated throw is submitted on a click alone. */
static inline int kbowling_turn_pointer(KBowlingTurn *t,KBowlingSession *s,int x,int y,unsigned held){
    if(!t||!s)return -1;
    if(t->phase!=KB_TURN_AIM||t->cpu)return 0;
    if(x<0||x>640||y<0||y>480)return 0;
    t->pointer_x=x;t->pointer_y=y;
    t->actor_position.x=(float)((double)1.3f*(x/640.0-.5)-.125);
    if(held&&!t->held)kbowling_throw_press(&t->gesture,x,y,t->now);
    if(t->held)kbowling_throw_drag(&t->gesture,x,y,t->now,s->difficulty);
    if(!held&&t->held&&kbowling_throw_release(&t->gesture)){
        memcpy(t->throw_plan.controls,t->gesture.controls,sizeof(t->throw_plan.controls));
        t->throw_plan.speed=t->gesture.speed;t->throw_plan.approach_frames=8;t->throw_plan.approach_repeats=3;t->throw_plan.release=t->user_release;
        t->actor_position=t->gesture.controls[0];t->actor_position.x-=.125f;t->actor_position.z=-5;
        t->phase=KB_TURN_APPROACH;t->step=t->clock=0;
    }
    t->held=held!=0;return 0;
}
/* 4ca850/4cbbd0 approach advances 0.1 per20ms. The following native action
   stream gates actual release on -3; the ball must not roll during approach.
   Caller handles sounds, reactions/AX, score and sweep after DONE. */
static inline int kbowling_turn_tick(KBowlingTurn *t,KBowlingSession *s,unsigned elapsed){
    if(!t||!s||!s->match.active)return -1;
    t->now+=elapsed;
    if(t->phase==KB_TURN_RACK){
        int status=kbowling_rack_tick(&t->rack,elapsed);if(status<0)return -1;
        if(!status&&kbowling_turn_setup(t,s))return -1;
    }else if(t->phase==KB_TURN_AIM){
        if(t->held)kbowling_throw_drag(&t->gesture,t->pointer_x,t->pointer_y,t->now,s->difficulty);
    }else if(t->phase==KB_TURN_APPROACH){
        t->clock+=elapsed;
        if(t->clock>=20){
            t->clock=0;t->sprite=(int)(t->step/t->throw_plan.approach_repeats);t->actor_position.z+=.1f;
            if(++t->step==t->throw_plan.approach_frames*t->throw_plan.approach_repeats){
                t->throw_sound=1;kbowling_action_begin(&t->actor,t->throw_plan.release,t->now);t->phase=KB_TURN_RELEASE;
            }
        }
    }else if(t->phase==KB_TURN_RELEASE){
        if(kbowling_action_tick(&t->actor,t->now)<0)return -1;
        if(t->actor.sprite>=0)t->sprite=t->actor.sprite;
        if(t->actor.released){
            if(kbowling_session_throw(s,t->throw_plan.controls,t->throw_plan.speed))return -1;
            t->phase=KB_TURN_FLIGHT;
        }else if(!t->actor.active)return -1;
    }else if(t->phase==KB_TURN_FLIGHT){
        if(!kbowling_world_tick(&s->world,elapsed)){t->phase=KB_TURN_SETTLE;t->clock=0;}
    }else if(t->phase==KB_TURN_SETTLE){
        t->clock+=elapsed;if(t->clock>=500)t->phase=KB_TURN_DONE;
    }else if(t->phase!=KB_TURN_DONE)return -1;
    return t->phase==KB_TURN_DONE?0:1;
}
#endif

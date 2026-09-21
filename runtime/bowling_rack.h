#ifndef KISAKU_BOWLING_RACK_H
#define KISAKU_BOWLING_RACK_H
#include "bowling_world.h"
/* 4d3490 / 4d3900: private render positions are separate from physics.
   Stored surviving pins are raised before the sweeper passes, then lowered. */
typedef struct {
    KBowlingVector pins[10],saved[10],machine,sweeper;
    unsigned visible[10],survives[10],pose[10],pose_elapsed[10];
    unsigned phase,step,clock,clear_all,preserve,machine_visible,sweeper_visible,sweeper_tile;
} KBowlingRack;
enum {KB_RACK_DONE,KB_RACK_LOWER,KB_RACK_RAISE_MACHINE,KB_RACK_CAPTURE,
    KB_RACK_LIFT,KB_RACK_SWEEP_LOWER,KB_RACK_SWEEP,KB_RACK_SWEEP_RAISE,
    KB_RACK_RESTORE,KB_RACK_RETRACT};
static inline int kbowling_rack_begin(KBowlingRack *r,const KBowlingWorld *w,const float positions[10][3],unsigned clear,unsigned clear_all){
    if(!r||!w||!positions||w->active)return -1;
    KBowlingRack next={.clear_all=clear_all,.phase=clear?KB_RACK_SWEEP_LOWER:KB_RACK_LOWER};
    for(unsigned i=0;i<10;i++){
        next.pins[i]=kbowling_pin_position(&w->pins[i]);next.visible[i]=w->present[i];
        next.pose[i]=w->pose[i];next.pose_elapsed[i]=w->pose_elapsed[i];
        next.survives[i]=w->present[i]&&!w->fallen[i];next.saved[i]=next.pins[i];
        if(next.survives[i])next.preserve=1;
        if(!clear){next.pins[i]=(KBowlingVector){positions[i][0],(float)(positions[i][1]+(double).6f),positions[i][2]};next.visible[i]=1;next.pose[i]=next.pose_elapsed[i]=0;}
    }
    next.machine=(KBowlingVector){positions[0][0],(float)(positions[0][1]+(double).6f),positions[0][2]};
    next.sweeper=next.machine;
    if(!clear)next.machine_visible=1;
    else if(!clear_all&&next.preserve){next.phase=KB_RACK_CAPTURE;next.machine_visible=1;}
    else next.sweeper_visible=1;
    *r=next;return 0;
}
/* Each native wait renders once, then waits 20ms (sweep steps:100ms).
   A late pump advances one step, rather than skipping rendered frames. */
static inline int kbowling_rack_tick(KBowlingRack *r,unsigned elapsed){
    if(!r||!r->phase)return 0;
    r->clock+=elapsed;unsigned wait=r->phase==KB_RACK_SWEEP?100:20;
    if(r->clock<wait)return 1;
    r->clock=0;
    switch(r->phase){
    case KB_RACK_LOWER:
        for(unsigned i=0;i<10;i++)r->pins[i].y=(float)(r->pins[i].y-(double).02f);
        r->machine=r->pins[0];
        if(++r->step==30){r->step=0;r->phase=KB_RACK_RAISE_MACHINE;}break;
    case KB_RACK_RAISE_MACHINE:
        r->machine.y+=.02f;if(++r->step==30){r->machine_visible=0;r->phase=KB_RACK_DONE;}break;
    case KB_RACK_CAPTURE:
        r->machine.y-=.02f;if(++r->step==30){r->step=0;r->phase=KB_RACK_LIFT;}break;
    case KB_RACK_LIFT:
        for(unsigned i=0;i<10;i++)if(r->survives[i])r->pins[i].y+=.02f;
        r->machine.y+=.02f;
        if(++r->step==30){r->step=0;r->phase=KB_RACK_SWEEP_LOWER;r->sweeper_visible=1;}break;
    case KB_RACK_SWEEP_LOWER:
        r->sweeper.y=(float)(r->sweeper.y-(double).02f);
        if(++r->step==30){r->step=0;r->phase=KB_RACK_SWEEP;}break;
    case KB_RACK_SWEEP:{
        static const float z[3]={19.788000106811523f,21.288000106811523f,22.788000106811523f};
        for(unsigned i=0;i<10;i++)if(r->visible[i]){
            if(!r->survives[i]||r->clear_all){
                if(r->pins[i].z<z[r->step]){
                    r->pins[i].z=z[r->step];
                    if(r->survives[i]){r->pose[i]=kbowling_pin_pose((KBowlingVector){1,0,0},0);r->pose_elapsed[i]=0;r->survives[i]=0;}
                }
                r->pose_elapsed[i]+=100;
            }
        }
        r->sweeper_tile=r->step;
        if(++r->step==3){
            for(unsigned i=0;i<10;i++)if(!r->survives[i])r->visible[i]=0;
            r->step=0;r->phase=KB_RACK_SWEEP_RAISE;
        }break;
    }
    case KB_RACK_SWEEP_RAISE:
        r->sweeper.y=(float)(r->sweeper.y+(double).02f);
        if(++r->step==30){r->step=0;r->sweeper_visible=0;r->sweeper_tile=0;r->phase=!r->clear_all&&r->preserve?KB_RACK_RESTORE:KB_RACK_DONE;}
        break;
    case KB_RACK_RESTORE:
        for(unsigned i=0;i<10;i++)if(r->survives[i]){
            r->pins[i].y=r->step<29?(float)(r->pins[i].y-(double).02f):0;
        }
        r->machine.y-=.02f;
        if(++r->step==30){r->step=0;r->phase=KB_RACK_RETRACT;}break;
    case KB_RACK_RETRACT:
        r->machine.y+=.02f;if(++r->step==30){r->phase=KB_RACK_DONE;r->machine_visible=0;}break;
    default:return -1;
    }
    return r->phase!=KB_RACK_DONE;
}
#endif

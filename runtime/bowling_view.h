#ifndef KISAKU_BOWLING_VIEW_H
#define KISAKU_BOWLING_VIEW_H
#include <stdlib.h>
#include "bowling_game.h"
#include "bowling_panels.h"
typedef struct {
    KBowlingArt lane;
    const KImage *user,*cpu,*results,*tutorial;
} KBowlingView;
/* Portable solid-pen raster for the two native guide segments. Clip to the
   private 200x260 window at (402,192); native pens are black9 then red7. */
static inline void kbowling_guide_segment(KImage *dst,int x0,int y0,int x1,int y1,int width,unsigned red){
    x0=415+194*x0/200;x1=415+194*x1/200;y0=205+254*y0/260;y1=205+254*y1/260;
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){
        for(int y=y0-width/2;y<=y0+width/2;y++)for(int x=x0-width/2;x<=x0+width/2;x++)if(x>=402&&x<602&&y>=192&&y<452){
            uint8_t *p=dst->pixels+y*dst->stride+x*4;p[0]=p[1]=0;p[2]=(uint8_t)red;
        }
        if(x0==x1&&y0==y1)break;
        int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}
    }
}
static inline void kbowling_draw_aim(KImage *dst,const KImage *parts,const KBowlingTurn *turn){
    const KBowlingThrow *t=&turn->gesture;
    int lane=t->held||t->ready?t->start_x:kbowling_clamp(turn->pointer_x,0,640)*180/640;
    int rise=t->held||t->ready?t->distance:0;if(lane>177)lane=177;if(rise>238)rise=238;
    kbowling_draw_rect(dst,407+lane,450,parts,260,0,12,12,1);
    kbowling_draw_rect(dst,394,435-rise,parts,248,0,12,12,1);
    if(t->held||t->ready)for(unsigned pass=0;pass<2;pass++){
        int width=pass?7:9;unsigned red=pass?255:0;
        if(t->crossed){
            kbowling_guide_segment(dst,t->start_x,239,t->mid_x,119,width,red);
            kbowling_guide_segment(dst,t->mid_x,119,t->lane_x,240-t->distance,width,red);
        }else kbowling_guide_segment(dst,t->start_x,239,t->lane_x,240-t->distance,width,red);
    }
}
static inline int kbowling_draw_game(KImage *dst,KBowlingView view,const KBowlingGame *g,int focus){
    if(!g)return -1;
    KBowlingSession shown=g->session;
    const KBowlingRack *rack=g->phase==KB_GAME_SWEEP?&g->sweep:
        g->phase==KB_GAME_TURN&&g->turn.phase==KB_TURN_RACK?&g->turn.rack:NULL;
    if(rack)for(unsigned i=0;i<10;i++){
        shown.world.present[i]=rack->visible[i];shown.world.pose[i]=rack->pose[i];shown.world.pose_elapsed[i]=rack->pose_elapsed[i];
    }
    if(g->phase==KB_GAME_PERFECT||g->phase==KB_GAME_RESULTS)memset(shown.world.present,0,sizeof(shown.world.present));
    unsigned ball=g->phase==KB_GAME_TURN&&(g->turn.phase==KB_TURN_FLIGHT||g->turn.phase==KB_TURN_SETTLE);
    if(kbowling_draw(dst,view.lane,&shown,shown.world.pose,rack?rack->pins:NULL,ball,1))return -1;
    if(rack){
        if(!kbowling_image_fits(view.lane.pins,624,128))return -1;
        KBowlingCamera camera=kbowling_camera();
        for(unsigned i=0;i<2;i++)if(i?rack->sweeper_visible:rack->machine_visible){
            int x,y;if(kbowling_project(&camera,i?rack->sweeper:rack->machine,0,&x,&y))return -1;
            int sx=i?96+88*(3+(int)rack->sweeper_tile):272,sy=i?56:92;
            kbowling_draw_rect(dst,x-32,y-8,view.lane.pins,sx,sy,88,36,1);
        }
        kbowling_draw_rect(dst,0,120,view.lane.pins,96,0,386,53,0);
    }
    if((g->phase==KB_GAME_TURN&&g->turn.phase!=KB_TURN_RACK)||g->phase==KB_GAME_REACTION){
        int sprite=g->phase==KB_GAME_REACTION&&g->reaction.sprite>=0?g->reaction.sprite:g->turn.sprite;
        if(kbowling_draw_actor(dst,g->turn.cpu?view.cpu:view.user,g->turn.actor_position,sprite,g->turn.cpu))return -1;
        /* Native priorities: actor14, guide15, score16. */
        kbowling_draw_rect(dst,386,126,view.lane.parts,0,0,248,344,1);
        kbowling_draw_score(dst,view.lane.parts,&shown.match);
    }
    double speed=g->turn.phase==KB_TURN_AIM?g->turn.gesture.speed:g->turn.throw_plan.speed;
    if(kbowling_draw_guide(dst,view.lane.parts,&shown.world,speed))return -1;
    if(g->phase==KB_GAME_TURN&&!g->turn.cpu&&g->turn.phase!=KB_TURN_RACK)kbowling_draw_aim(dst,view.lane.parts,&g->turn);
    if(g->phase==KB_GAME_TUTORIAL)return kbowling_draw_tutorial(dst,view.tutorial,g->tutorial_page,focus,0);
    if(g->phase==KB_GAME_PERFECT){
        if(!kbowling_image_fits(view.lane.parts,512,688))return -1;
        kbowling_draw_rect(dst,64,192,view.lane.parts,0,528,512,96,1);
        int sx=(int)(g->perfect_elapsed/100%6)*64;
        kbowling_draw_rect(dst,0,208,view.lane.parts,sx,624,64,64,1);
        kbowling_draw_rect(dst,576,208,view.lane.parts,sx,624,64,64,1);
    }
    if(g->phase==KB_GAME_RESULTS)return kbowling_draw_results(dst,view.results,&g->session.match,g->session.match.finished);
    return 0;
}
#endif

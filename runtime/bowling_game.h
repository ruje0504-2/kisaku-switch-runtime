#ifndef KISAKU_BOWLING_GAME_H
#define KISAKU_BOWLING_GAME_H
#include "bowling_turn.h"
/* Modal orchestration above the physical throw. Score commits only after
   reaction presentation; the old pin positions survive until sweeping ends. */
enum {KB_GAME_IDLE,KB_GAME_TUTORIAL,KB_GAME_TURN,KB_GAME_REACTION,KB_GAME_SWEEP,KB_GAME_PERFECT,KB_GAME_RESULTS,KB_GAME_RETURN};
typedef struct {
    KBowlingSession session,next;
    KBowlingTurn turn;
    KBowlingRack sweep;
    KBowlingActionPlayer reaction;
    const KBowlingActionStream *reactions;
    KBowlingActionStream user_release;
    unsigned phase,now,tutorial_page,score_ready,ax_wait,perfect_elapsed,gutter_reacted;
    int ax_request,sound_request;
} KBowlingGame;
static inline int kbowling_game_begin(KBowlingGame *g,int mode,const int32_t roles[4],unsigned difficulty,
    uint32_t seed,KBowlingTables tables,const KBowlingActionStream reactions[30],KBowlingActionStream release){
    if(!g||!reactions||!release.commands||!release.count)return -1;
    KBowlingGame next={.phase=KB_GAME_TUTORIAL,.reactions=reactions,.user_release=release,.ax_request=-1,.sound_request=-1};
    if(kbowling_session_create(&next.session,mode,roles,difficulty,seed,tables))return -1;
    *g=next;return 0;
}
static inline int kbowling_game_confirm(KBowlingGame *g){
    if(!g)return -1;
    if(g->phase==KB_GAME_TUTORIAL){
        if(kbowling_turn_begin(&g->turn,&g->session,g->user_release))return -1;
        g->phase=KB_GAME_TURN;
    }else if(g->phase==KB_GAME_RESULTS)g->phase=KB_GAME_RETURN;
    return 0;
}
static inline int kbowling_game_second_half(KBowlingGame *g){
    if(!g||g->phase!=KB_GAME_RETURN)return -1;
    KBowlingGame next=*g;
    if(kbowling_session_second_half(&next.session)||kbowling_turn_begin(&next.turn,&next.session,next.user_release))return -1;
    next.phase=KB_GAME_TURN;*g=next;return 0;
}
static inline int kbowling_game_react(KBowlingGame *g){
    KBowlingSession *s=&g->session;
    unsigned pins=kbowling_world_count(&s->world),ball=s->match.ball,frame=s->match.frame;
    const KBowlingScore *score=&s->match.scores[s->match.player];
    unsigned reaction=0;
    if(!s->same_rack&&pins==10){
        unsigned streak=0;
        if(frame&&kbowling_score_pins(score,frame-1,0)==10){streak=1;if(frame>1&&kbowling_score_pins(score,frame-2,0)==10)streak=2;}
        g->ax_request=(int)streak;g->sound_request=23;reaction=2;
    }else if(s->same_rack&&ball&&pins==s->match.rack_remaining){g->ax_request=3;g->sound_request=22;reaction=1;}
    if(reaction){
        unsigned index=s->match.player?s->match.characters[0]*3+reaction-1:27+reaction-1;
        kbowling_action_begin(&g->reaction,g->reactions[index],g->now);
    }else if(!g->gutter_reacted){memset(&g->reaction,0,sizeof(g->reaction));g->reaction.sprite=-1;}
    g->ax_wait=g->ax_request>=0;g->phase=KB_GAME_REACTION;return 0;
}
static inline int kbowling_game_tick(KBowlingGame *g,unsigned elapsed){
    if(!g)return -1;
    g->now+=elapsed;
    if(g->phase==KB_GAME_TURN){
        int rc=kbowling_turn_tick(&g->turn,&g->session,elapsed);if(rc<0)return -1;
        if(g->session.world.gutter&&!g->gutter_reacted){
            unsigned index=g->session.match.player?g->session.match.characters[0]*3+2:29;
            kbowling_action_begin(&g->reaction,g->reactions[index],g->now);g->gutter_reacted=1;
        }
        if(g->reaction.active){
            if(kbowling_action_tick(&g->reaction,g->now)<0)return -1;
            if(g->reaction.sprite>=0)g->turn.sprite=g->reaction.sprite;
        }
        if(!rc)return kbowling_game_react(g);
    }else if(g->phase==KB_GAME_REACTION){
        if(g->reaction.active&&kbowling_action_tick(&g->reaction,g->now)<0)return -1;
        if(!g->reaction.active&&!g->ax_wait){
            g->next=g->session;
            if(kbowling_session_score(&g->next)<0)return -1;
            unsigned clear_all=!g->next.match.active||!g->next.same_rack;
            if(kbowling_rack_begin(&g->sweep,&g->session.world,g->session.tables.positions,1,clear_all))return -1;
            /* The scores are visible before the next rack is installed. */
            memcpy(g->session.match.scores,g->next.match.scores,sizeof(g->session.match.scores));
            g->score_ready=1;g->phase=KB_GAME_SWEEP;
        }
    }else if(g->phase==KB_GAME_SWEEP){
        int rc=kbowling_rack_tick(&g->sweep,elapsed);if(rc<0)return -1;
        if(!rc){
            g->session=g->next;g->score_ready=0;g->gutter_reacted=0;memset(&g->reaction,0,sizeof(g->reaction));g->reaction.sprite=-1;
            if(g->session.match.active){
                if(kbowling_turn_begin(&g->turn,&g->session,g->user_release))return -1;
                g->phase=KB_GAME_TURN;
            }else if(g->session.match.finished&&g->session.match.result==2){
                g->phase=KB_GAME_PERFECT;g->perfect_elapsed=0;g->sound_request=26;
            }else g->phase=KB_GAME_RESULTS;
        }
    }else if(g->phase==KB_GAME_PERFECT){
        g->perfect_elapsed+=elapsed;if(g->perfect_elapsed>=4000)g->phase=KB_GAME_RESULTS;
    }
    return 0;
}
#endif

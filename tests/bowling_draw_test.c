#include "bowling_view.h"
#include "ai6arc.h"
#include "akb.h"
#include "../build/bowling_tables.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static KImage load(Ai6Archive *arc,const char *name){uint8_t *data=NULL;size_t size=0;KImage im={0};assert(!ai6_read_named(arc,name,&data,&size));assert(!akb_decode(data,size,&im));free(data);return im;}
int main(int argc,char **argv){
    assert(argc==2||argc==3);char path[4096];snprintf(path,sizeof(path),"%s/layer.arc",argv[1]);Ai6Archive arc={0};assert(!ai6_open(&arc,path));
    KImage bg=load(&arc,"bow_bg.akb"),pin=load(&arc,"bow_pin.akb"),ball=load(&arc,"bow_ball.akb"),parts=load(&arc,"bow_pt.akb");
    KImage tutorial=load(&arc,"tutbow.akb"),results=load(&arc,"bow_sr.akb");
    KImage screen={.width=640,.height=480,.stride=640*4,.pixels=calloc(480,640*4)};assert(screen.pixels);
    KBowlingTables tables={kisaku_bowling_pin_positions,kisaku_bowling_aim,kisaku_bowling_lanes,kisaku_bowling_throws,kisaku_bowling_profiles,kisaku_bowling_spread};
    KBowlingSession s;int32_t roles[4]={0,1,2,3};assert(!kbowling_session_create(&s,1,roles,1,123,tables));
    unsigned poses[10]={0};KBowlingArt art={&bg,&pin,&ball,&parts};
    assert(!kbowling_draw(&screen,art,&s,poses,NULL,0,1));
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)assert(screen.pixels[y*screen.stride+x*4+3]==255);
    /* Native guide source(10,10) -> screen(396,136), independent of camera. */
    assert(!memcmp(screen.pixels+136*screen.stride+396*4,parts.pixels+10*parts.stride+10*4,3));
    uint8_t *saved=malloc(480*screen.stride);assert(saved);memcpy(saved,screen.pixels,480*screen.stride);
    KImage invalid=parts;invalid.height=100;art.parts=&invalid;
    assert(kbowling_draw(&screen,art,&s,poses,NULL,0,1)==-1&&!memcmp(saved,screen.pixels,480*screen.stride));
    art.parts=&parts;
    for(unsigned player=0;player<2;player++)for(unsigned frame=0;frame<10;frame++){
        assert(!kbowling_score_set(&s.match.scores[player],frame,0,10,0,0));
        if(frame==9){assert(!kbowling_score_set(&s.match.scores[player],frame,1,10,0,0));assert(!kbowling_score_set(&s.match.scores[player],frame,2,10,0,0));}
    }
    assert(!kbowling_draw(&screen,art,&s,poses,NULL,0,1));assert(memcmp(saved,screen.pixels,480*screen.stride));
    if(argc==3){FILE *fp=fopen(argv[2],"wb");assert(fp);fprintf(fp,"P6\n640 480\n255\n");for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){const uint8_t*p=screen.pixels+y*screen.stride+x*4;uint8_t rgb[3]={p[2],p[1],p[0]};assert(fwrite(rgb,1,3,fp)==3);}assert(!fclose(fp));}
    assert(!kbowling_draw_guide(&screen,&parts,&s.world,11.112));
    assert(!kbowling_draw_tutorial(&screen,&tutorial,0,257,0));
    assert(kbowling_tutorial_hit(0,450,420)==257&&kbowling_tutorial_hit(1,450,420)==-1);
    assert(kbowling_tutorial_hit(1,120,420)==256&&kbowling_tutorial_hit(0,120,420)==-1);
    assert(kbowling_tutorial_hit(1,300,420)==0);
    assert(!kbowling_draw(&screen,art,&s,poses,NULL,0,1));
    assert(!kbowling_draw_tutorial(&screen,&tutorial,1,0,1));
    assert(!kbowling_draw_results(&screen,&results,&s.match,0));
    memcpy(saved,screen.pixels,480*screen.stride);
    assert(kbowling_draw_results(&screen,&results,&s.match,1)==-1&&!memcmp(saved,screen.pixels,480*screen.stride));
    for(unsigned player=2;player<5;player++)s.match.scores[player]=s.match.scores[0];
    assert(!kbowling_draw_results(&screen,&results,&s.match,1));
    KImage user=load(&arc,"bow_men.akb");
    KBowlingGame game;assert(!kbowling_game_begin(&game,1,roles,1,123,tables,kisaku_bowling_actions,kisaku_bowling_user_throw));
    KBowlingView view={art,&user,NULL,&results,&tutorial};
    game.phase=KB_GAME_TURN;game.turn.phase=KB_TURN_AIM;game.turn.pointer_x=320;game.turn.actor_position=(KBowlingVector){-.125f,0,-5};
    assert(!kbowling_draw_game(&screen,view,&game,0));
    game.turn.gesture=(KBowlingThrow){.held=1,.crossed=1,.start_x=90,.mid_x=100,.lane_x=120,.distance=220,.speed=9};
    assert(!kbowling_draw_game(&screen,view,&game,0));
    for(unsigned role=0;role<9;role++){
        char name[32];snprintf(name,sizeof(name),"bow_c%u.akb",role);KImage cpu=load(&arc,name);view.cpu=&cpu;game.turn.cpu=1;
        for(unsigned kind=0;kind<4;kind++){
            KBowlingActionStream stream=kind<3?kisaku_bowling_actions[role*3+kind]:kisaku_bowling_throws[role];
            for(size_t i=0;i<stream.count;i++)if(stream.commands[i].command>=0){
                game.turn.sprite=stream.commands[i].command;assert(!kbowling_draw_game(&screen,view,&game,0));
            }
        }
        rmt_free(&cpu);
    }
    game.phase=KB_GAME_PERFECT;game.session.match.finished=1;game.session.match.result=2;
    for(unsigned i=0;i<6;i++){game.perfect_elapsed=i*100;assert(!kbowling_draw_game(&screen,view,&game,0));}
    rmt_free(&user);
    rmt_free(&results);rmt_free(&tutorial);
    free(saved);rmt_free(&screen);rmt_free(&bg);rmt_free(&pin);rmt_free(&ball);rmt_free(&parts);ai6_close(&arc);
    puts("bowling-draw: real assets, score updates, opaque output and rejected truncated atlas passed");
}

#include "../build/bowling_tables.h"
int bootstrap_bowling_active(const KBootstrap *b){return b&&b->bowling_runtime&&b->bowling_runtime->active;}
static KBowlingView bowling_view(KBootstrap *b,KBowlingRuntime *r){
    return (KBowlingView){{&r->background,&b->layers[1],&b->layers[2],&b->layers[5]},
        &b->layers[9],&b->layers[8],&b->layers[3],&r->tutorial};
}
static void bowling_free(KBootstrap *b){
    kbowling_runtime_free(b->bowling_runtime);b->bowling_runtime=NULL;
    for(unsigned i=0;i<27;i++){free(b->bowling_effects[i].pcm);memset(&b->bowling_effects[i],0,sizeof(b->bowling_effects[i]));}
}
static void bowling_sound(KBootstrap *b,unsigned id){
    if(id<27){KEffectTrack *t=&b->bowling_effects[id];t->read=t->clock_position=0;}
}
static void bowling_ax_draw(const uint32_t d[7],unsigned cell,void *context){
    (void)cell;KBootstrap *b=context;KBowlingRuntime *r=b->bowling_runtime;
    /* CBowling secondary virtual48 (4d47c0): layer4 -> page0 RGB. */
    if(d[0]){error(b,"bowling AX descriptor unsupported");return;}
    KImage *src=&b->layers[4];
    if(d[1]>src->width||d[2]>src->height||d[3]>src->width-d[1]||d[4]>src->height-d[2]||
       d[5]>640||d[6]>480||d[3]>640-d[5]||d[4]>480-d[6]){error(b,"bowling AX rectangle outside surface");return;}
    kbowling_draw_rect(&r->effect,(int)d[5],(int)d[6],src,(int)d[1],(int)d[2],(int)d[3],(int)d[4],0);
}
static int bowling_present(KBootstrap *b){
    KBowlingRuntime *r=b->bowling_runtime;if(!r||!r->active)return 0;
    if(kbowling_draw_game(&b->layers[0],bowling_view(b,r),&r->game,r->focus))return error(b,"bowling surface or sprite invalid");
    if(r->game.ax_wait)for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *p=r->effect.pixels+y*r->effect.stride+x*4;
        if(p[3])memcpy(b->layers[0].pixels+y*b->layers[0].stride+x*4,p,3);
    }
    return 0;
}
static int bowling_begin(KBootstrap *b,const int32_t args[5]){
    if(bootstrap_bowling_active(b))return error(b,"bowling modal already active (arguments preserved)");
    if(!kbowling_image_fits(&b->layers[0],640,480)||!kbowling_image_fits(&b->layers[1],624,288)||
       !kbowling_image_fits(&b->layers[2],640,128)||!kbowling_image_fits(&b->layers[3],492,432)||
       !kbowling_image_fits(&b->layers[4],640,480)||!kbowling_image_fits(&b->layers[5],640,524)||
       !kbowling_image_fits(&b->layers[9],1152,900)||
       (args[0]&&!kbowling_image_fits(&b->layers[8],1536,208)))return error(b,"bowling borrowed layers missing (arguments preserved)");
    KBowlingRuntime *r=calloc(1,sizeof(*r));if(!r)return error(b,"bowling allocation failed");
    KBowlingTables tables={kisaku_bowling_pin_positions,kisaku_bowling_aim,kisaku_bowling_lanes,kisaku_bowling_throws,kisaku_bowling_profiles,kisaku_bowling_spread};
    int difficulty=option(b,"Difficult","AllMiniGame",1);if(difficulty<0)difficulty=0;if(difficulty>2)difficulty=2;
    if(kbowling_game_begin(&r->game,args[0],args+1,(unsigned)difficulty,(uint32_t)time(NULL),tables,kisaku_bowling_actions,kisaku_bowling_user_throw)){
        kbowling_runtime_free(r);return error(b,"bowling constructor values invalid (arguments preserved)");
    }
    r->background=(KImage){0,0,640,480,2560,malloc(640*480*4)};
    r->effect=(KImage){0,0,640,480,2560,calloc(640*480,4)};
    uint8_t *data=NULL;size_t size=0;
    if(!r->background.pixels||!r->effect.pixels||read_named(&b->images,"tutbow.akb",&data,&size)||rmt_decode(data,size,&r->tutorial)){
        free(data);kbowling_runtime_free(r);return error(b,"bowling tutorial allocation/decode failed (arguments preserved)");
    }
    free(data);data=NULL;
    for(unsigned y=0;y<480;y++)memcpy(r->background.pixels+y*2560,b->layers[0].pixels+y*b->layers[0].stride,2560);
    if(read_named(&b->data,"bow.ax",&data,&size)||!ax_load(&r->ax,"bow.ax",data,size)){
        free(data);kbowling_runtime_free(r);return error(b,"bowling AX resource invalid (arguments preserved)");
    }
    free(data);
    KEffectTrack sounds[27]={0};
    static const char *tail[]={"bowthrow.wav","bowgut.wav","Claps.wav","claps2.wav","buuuuu.wav","bowall.wav","bowperf.wav"};
    for(unsigned i=0;i<27;i++){
        const char *name=i<10?"bowh.wav":i<20?"bowl.wav":tail[i-20];
        if(effect_decode(b,&sounds[i],name)){
            for(unsigned n=0;n<27;n++)free(sounds[n].pcm);
            kbowling_runtime_free(r);return -1;
        }
        sounds[i].read=sounds[i].clock_position=sounds[i].size;
    }
    if(kbowling_release(&b->bowling,&b->current_bowling)){
        for(unsigned n=0;n<27;n++)free(sounds[n].pcm);
        kbowling_runtime_free(r);return error(b,"bowling ownership invalid (arguments preserved)");
    }
    bowling_free(b);b->bowling_runtime=r;memcpy(b->bowling_effects,sounds,sizeof(sounds));
    const unsigned layers[]={5,3,1,2,9,8};for(unsigned i=0;i<6;i++)b->bowling.slots[17+i]=(KBowlingResource){&b->layers[layers[i]],NULL};
    b->current_bowling=&b->bowling;r->active=1;r->focus=0;r->pointer_x=320;r->pointer_y=440;
    return bowling_present(b);
}
static int bowling_fade(KBootstrap *b,int fade_in);
static int bowling_second(KBootstrap *b){
    KBowlingRuntime *r=b->bowling_runtime;
    if(!r||r->active||kbowling_game_second_half(&r->game))return error(b,"bowling second half unavailable (arguments preserved)");
    r->active=1;r->second_half=1;r->closing=0;
    if(bowling_fade(b,1))return -1;
    return bowling_present(b);
}
static int bowling_fade(KBootstrap *b,int fade_in){
    KBowlingRuntime *r=b->bowling_runtime;
    if(!b->fade_surface.pixels){
        b->fade_surface=(KImage){0,0,640,480,2560,calloc(640*480,4)};
        if(!b->fade_surface.pixels)return error(b,"bowling fade allocation failed");
    }
    for(unsigned i=0;i<640*480;i++){memset(b->fade_surface.pixels+i*4,0,3);b->fade_surface.pixels[i*4+3]=255;}
    r->fade_in=fade_in!=0;r->fade_steps=message_effect_steps(b,32);r->fade_tick=r->fade_clock=0;
    b->fade_steps=0;b->fade_visible=1;b->fade_alpha=fade_in?255:0;
    if(!r->fade_steps){b->fade_alpha=fade_in?0:255;b->fade_visible=!fade_in;}
    return 0;
}
static void bowling_confirm(KBootstrap *b){
    KBowlingRuntime *r=b->bowling_runtime;if(!r||!r->active||r->fade_steps||r->closing)return;
    if(r->game.phase==KB_GAME_TUTORIAL&&r->focus==257)r->game.tutorial_page=1;
    else if(r->game.phase==KB_GAME_TUTORIAL&&r->focus==256)r->game.tutorial_page=0;
    else if(kbowling_game_confirm(&r->game))error(b,"bowling confirmation failed");
}
void bootstrap_bowling_pointer(KBootstrap *b,int x,int y,unsigned held){
    KBowlingRuntime *r=b?b->bowling_runtime:NULL;if(!r||!r->active||r->fade_steps||r->closing)return;
    unsigned pressed=held&&!r->input_held;r->input_held=held!=0;
    r->pointer_x=x;r->pointer_y=y;
    if(r->game.phase==KB_GAME_TUTORIAL){
        r->focus=kbowling_tutorial_hit(r->game.tutorial_page,x,y);
        if(pressed&&r->focus>=0)bowling_confirm(b);
    }else if(r->game.phase==KB_GAME_RESULTS){if(pressed)bowling_confirm(b);}
    else if(kbowling_turn_pointer(&r->game.turn,&r->game.session,kbowling_clamp(x,0,640),kbowling_clamp(y,0,480),held))error(b,"bowling gesture failed");
}
static void bowling_frame(KBootstrap *b){
    KBowlingRuntime *r=b->bowling_runtime;if(!r||!r->active)return;
    KBowlingGame *g=&r->game;
    if(r->fade_steps){
        r->fade_clock+=1000;
        while(r->fade_clock>=900&&r->fade_steps){
            r->fade_clock-=900;r->fade_tick++;
            unsigned a=255*r->fade_tick/r->fade_steps;b->fade_alpha=r->fade_in?255-a:a;
            if(r->fade_tick==r->fade_steps){r->fade_steps=0;if(r->fade_in)b->fade_visible=0;}
        }
        return;
    }
    unsigned before_gutter=g->session.world.gutter,before_loud=g->session.world.loud_hit;
    if(kbowling_game_tick(g,b->frames%3==0?16:17)){error(b,"bowling update failed");return;}
    if(g->turn.throw_sound){bowling_sound(b,20);g->turn.throw_sound=0;}
    if(g->phase==KB_GAME_TURN){
        if(g->session.world.gutter&&!before_gutter){bowling_sound(b,21);bowling_sound(b,24);}
        /* 4d12b0 calls the pin-indexed bowh voice only if not already
           playing. The computed soft-hit alternative is unused in the EXE. */
        if(g->turn.phase==KB_TURN_FLIGHT)for(unsigned pin=0;pin<10;pin++)
            if((g->session.world.ball_hit_mask&(1u<<pin))&&b->bowling_effects[pin].clock_position>=b->bowling_effects[pin].size)bowling_sound(b,pin);
        if(g->session.world.loud_hit&&!before_loud)bowling_sound(b,25);
    }
    if(g->sound_request>=0){bowling_sound(b,(unsigned)g->sound_request);g->sound_request=-1;}
    if(g->ax_request>=0){
        unsigned index=(unsigned)g->ax_request;struct ax_cell *c=&r->ax.cells[index];uint32_t start=c->start;
        memset(c,0,sizeof(*c));c->start=start;c->state=AX_STOPPED;
        memset(r->effect.pixels,0,640*480*4);r->clock=0;
        if(!ax_control(&r->ax,1,0,index)){error(b,"bowling reaction AX track invalid");return;}
        r->ax.wait_cell=index+1;g->ax_request=-1;
    }
    if(g->ax_wait){
        r->clock+=1000;
        while(r->clock>=1200){r->clock-=1200;if(!ax_tick_native(&r->ax,r->events,bowling_ax_draw,b)){error(b,"bowling reaction AX invalid");return;}}
        if(!ax_waiting(&r->ax))g->ax_wait=0;
    }
    if(g->phase==KB_GAME_RETURN){
        if(!r->closing){r->closing=1;(void)bowling_fade(b,0);return;}
        r->active=0;
        if(r->second_half){
            int result=g->session.match.result;
            if(kvm_push(b->vm,(KValue){result,NULL})){error(b,"bowling result allocation failed");return;}
            bowling_free(b);(void)kbowling_release(&b->bowling,&b->current_bowling);
        }
    }
}

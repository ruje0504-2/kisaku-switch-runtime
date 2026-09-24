/* Native scene-replay menu: 486d60 positions/atlases, 4873f0 hover fade. */
static const unsigned replay_positions[3][11][2]={
    {{40,64},{168,64},{296,64},{424,64},{40,192},{232,192},{424,192},{40,318},{168,318},{296,318},{424,318}},
    {{40,64},{232,64},{424,64},{40,192},{232,192},{424,192},{40,318},{232,318},{424,318}},
    {{40,64},{232,64},{424,64},{232,192},{40,318},{232,318},{424,318}}
};
/* Uneven native rows (4/3/4, 3/3/3, 3/1/3): choose the nearest
   thumbnail in the requested row/direction without changing character. */
static unsigned replay_grid_move(unsigned group,unsigned current,int action){
    unsigned count=bootstrap_replay_count(group);
    if(group>=5||current>=count||action<2||action>5)return current;
    const unsigned (*positions)[2]=replay_positions[group<3?0:group-2];
    unsigned best=current;int primary=100000,secondary=100000;
    for(unsigned i=0;i<count;i++){
        int dx=(int)positions[i][0]-(int)positions[current][0];
        int dy=(int)positions[i][1]-(int)positions[current][1],along,across;
        if(action==4||action==5){if(dy||(action==4?dx>=0:dx<=0))continue;along=abs(dx);across=0;}
        else {if(action==2?dy>=0:dy<=0)continue;along=abs(dy);across=abs(dx);}
        if(along<primary||(along==primary&&across<secondary)){best=i;primary=along;secondary=across;}
    }
    return best;
}
static const unsigned replay_frames[5][11]={
    {4,4,5,4,4,3,3,4,4,3,3},{4,3,4,3,4,3,3,4,3,3,3},
    {3,3,3,4,4,3,4,5,3,3,3},{3,3,3,4,3,3,3,4,3},{4,3,3,3,4,3,3}
};
static void replay_panel_clock(MessagePanel *p,unsigned frames,uint64_t now){
    if(now<p->replay_next)return;
    if(now-p->replay_next>10000){p->replay_next=now+15;return;}
    while(now>p->replay_next){
        p->replay_alpha+=6;p->replay_next+=16;
        if(p->replay_alpha>=255){p->replay_from=p->replay_to;p->replay_to=(p->replay_to+1)%frames;p->replay_alpha=0;p->replay_next+=500;}
    }
}
static int replay_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r,uint64_t now){
    if(p->back>=5||p->selected>=bootstrap_replay_count(p->back))return -1;
    unsigned layout=p->back<3?0:p->back-2,count=bootstrap_replay_count(p->back);
    if(p->cg_cached!=5||p->cg_group!=p->back){
        const char *backgrounds[3]={"sp_ev_bg11.rmt","sp_ev_bg09.rmt","sp_ev_bg07.rmt"};
        const char *atlases[5]={"sp_ev_a.rmt","sp_ev_g.rmt","sp_ev_e.rmt","sp_ev_f.rmt","sp_ev_i.rmt"};
        SDL_Texture *next[3]={gallery_panel_texture(b,r,backgrounds[layout],640,480,0),gallery_panel_texture(b,r,atlases[p->back],p->back==0||p->back==2?600:480,count*90,0),gallery_panel_texture(b,r,"sp_cg_p.rmt",432,616,1)};
        if(!next[0]||!next[1]||!next[2]){for(unsigned i=0;i<3;i++)SDL_DestroyTexture(next[i]);return -1;}
        gallery_panel_clear(p);memcpy(p->cg_textures,next,sizeof(next));p->cg_cached=5;p->cg_group=p->back;p->replay_key=0;
    }
    unsigned key=1+p->back*16+p->selected;
    if(p->replay_key!=key){p->replay_key=key;p->replay_from=0;p->replay_to=1;p->replay_alpha=0;p->replay_next=now+15;}
    if(bootstrap_replay_unlocked(b,p->back,p->selected))replay_panel_clock(p,replay_frames[p->back][p->selected],now);
    gallery_panel_copy(r,p->cg_textures[0],0,0,640,480,0,0);
    for(unsigned i=0;i<count;i++){
        unsigned x=replay_positions[layout][i][0],y=replay_positions[layout][i][1];
        if(bootstrap_replay_unlocked(b,p->back,i)){
            unsigned frame=i==p->selected?p->replay_from:0;
            gallery_panel_copy(r,p->cg_textures[1],frame*120,i*90,120,90,x,y);
            if(i==p->selected){
                SDL_SetTextureBlendMode(p->cg_textures[1],SDL_BLENDMODE_BLEND);SDL_SetTextureAlphaMod(p->cg_textures[1],(Uint8)p->replay_alpha);
                gallery_panel_copy(r,p->cg_textures[1],p->replay_to*120,i*90,120,90,x,y);
                SDL_SetTextureAlphaMod(p->cg_textures[1],255);SDL_SetTextureBlendMode(p->cg_textures[1],SDL_BLENDMODE_NONE);
                gallery_panel_copy(r,p->cg_textures[2],144,250,120,90,x,y);
            }
        }
        if(i==p->selected){SDL_SetRenderDrawColor(r,245,208,80,255);SDL_Rect border={160+(int)x*3/2-2,(int)y*3/2-2,184,139};SDL_RenderDrawRect(r,&border);}
    }
    for(unsigned group=0;group<5;group++)gallery_panel_copy(r,p->cg_textures[2],group==p->back?96:0,148+group*48,48,48,560,48+group*56);
    gallery_panel_copy(r,p->cg_textures[2],0,96,96,52,532,388);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,12,18,28,225);SDL_Rect footer={160,676,960,44};SDL_RenderFillRect(r,&footer);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,245,245,245,255);char text[96];snprintf(text,sizeof(text),ui_string(b,"menu.replay.footer"),p->selected+1,count,ui_string(b,bootstrap_replay_unlocked(b,p->back,p->selected)?"common.available":"common.locked"));
    panel_text(b,r,200,680,p->status[0]?p->status:text);return 0;
}

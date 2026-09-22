static int music_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    if(p->cg_cached!=4){
        SDL_Texture *background=gallery_panel_texture(b,r,"sp_bgm.rmt",640,480,0);
        SDL_Texture *parts=gallery_panel_texture(b,r,"sp_bgm_p.rmt",584,640,2);
        if(!background||!parts){SDL_DestroyTexture(background);SDL_DestroyTexture(parts);return -1;}
        gallery_panel_clear(p);p->cg_textures[0]=background;p->cg_textures[1]=parts;p->cg_cached=4;
    }
    gallery_panel_copy(r,p->cg_textures[0],0,0,640,480,0,0);
    unsigned count=bootstrap_music_count();
    for(unsigned i=0;i<count;i++){
        unsigned state=!bootstrap_music_unlocked(b,i)?3:b->music_active&&!SDL_strcasecmp(b->audio_name,bootstrap_music_name(i))?2:p->selected==i?1:0;
        gallery_panel_copy(r,p->cg_textures[1],i/8*232,(i%8*4+state)*20,232,20,24,20+i*28);
    }
    if(p->selected<count&&!bootstrap_music_unlocked(b,p->selected)){SDL_SetRenderDrawColor(r,245,208,80,255);SDL_Rect focus={196,30+(int)p->selected*42,348,30};SDL_RenderDrawRect(r,&focus);}
    gallery_panel_copy(r,p->cg_textures[1],464,b->music_active?0:84,120,28,340,420);
    gallery_panel_copy(r,p->cg_textures[1],464,112,72,28,484,420);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,12,18,28,225);SDL_Rect footer={160,692,960,28};SDL_RenderFillRect(r,&footer);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,245,245,245,255);panel_text(r,200,698,p->status[0]?p->status:"Up/down: track  A: play  Y/Tab: stop  B: back");return 0;
}
/* 422050 / 422220: original animation-versus-still modal atlas. */
static int style_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    if(p->cg_cached!=3){SDL_Texture *texture=gallery_panel_texture(b,r,"sl_data2.rmt",504,68,2);if(!texture)return -1;gallery_panel_clear(p);p->cg_textures[0]=texture;p->cg_cached=3;}
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,0,0,0,128);SDL_Rect dim={160,0,960,720};SDL_RenderFillRect(r,&dim);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    gallery_panel_copy(r,p->cg_textures[0],0,0,288,68,180,208);
    for(unsigned i=0;i<2;i++)gallery_panel_copy(r,p->cg_textures[0],288+(p->selected==i?72:0),i*28,72,28,236+i*104,236);
    SDL_SetRenderDrawColor(r,245,245,245,255);panel_text(r,272,684,"Left/right: style  A: confirm  B: cancel");return 0;
}

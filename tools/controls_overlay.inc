/* Controller hints share the CName left-margin anchor. Cache glyph rendering
   and upload only when the controls change, not once per game frame. */
typedef struct {KImage image;SDL_Texture *texture;char text[512];unsigned replay;} ControlsOverlay;
static void controls_focus(SDL_Renderer *r,int x,int y,int w,int h){
    SDL_Rect box={160+x*3/2,y*3/2,w*3/2,h*3/2};
    for(int i=0;i<5;i++){
        SDL_SetRenderDrawColor(r,i==0||i==4?0:255,i==0||i==4?0:224,i==0||i==4?0:32,255);
        SDL_Rect edge={box.x+i,box.y+i,box.w-i*2,box.h-i*2};SDL_RenderDrawRect(r,&edge);
    }
}
static const char *controls_text(const MessagePanel *p,const SaveMenu *m,const KBootstrap *b){
    if(m->active&&m->live_character)return "controls.live_character";
    if(m->active)return m->character_detail?"controls.character_detail":"controls.character_list";
    switch(p->kind){
    case 14:return NULL; /* CName uses this same anchor and its own cache. */
    case 20:return scene_mode_hage((KBootstrap *)b)||p->direct_sub>=0?"controls.scene_replay_simple":"controls.scene_replay_characters";
    case 22:return !bootstrap_native_cg_categories(b)?"controls.cg_simple":"controls.cg_characters";
    case 10:if(p->viewing==2)return "controls.cg_viewer";
        if(p->viewing)return "controls.cg_variants";
        /* fall through */
    case 11:return "controls.replay_characters";
    case 9:return "controls.music";
    case 23:return "controls.video";
    case 5:return "controls.history";
    case 8:return "controls.settings";
    case 4:return "controls.navigation";
    case 16:return "controls.scene_navigation";
    default:if(p->kind)return "controls.generic";
    }
    if(b->area_active)return "controls.area";
    if(b->bonus52_active)return "controls.bonus_game";
    if(b->title.active||b->choice_active)return "controls.choice";
    if(b->scene_replay)return "controls.replay_menu";
    return NULL;
}
static void controls_overlay_draw(ControlsOverlay *c,MessagePanel *p,SaveMenu *m,KBootstrap *b,SDL_Renderer *r){
    const char *key=controls_text(p,m,b);
    if(c->replay&&!p->kind&&!m->active)key="controls.replay_story";
    if(key){
        const char *text=bootstrap_ui_text(b,key);
        if(strcmp(c->text,text)){
            SDL_DestroyTexture(c->texture);c->texture=NULL;rmt_free(&c->image);c->text[0]=0;
            c->image=(KImage){0,0,144,164,576,calloc(144u*164u,4)};
            if(c->image.pixels){ui_fill(&c->image,0,0,144,164,0x0c0f14);
                if(!ui_text(b,&c->image,text,0,0,144,164,0xffe9a0,0)){
                    c->texture=kimage_texture(r,&c->image);if(c->texture)snprintf(c->text,sizeof(c->text),"%s",text);
                }
            }
        }
        if(c->texture){SDL_Rect dst={8,420,144,164};SDL_RenderCopy(r,c->texture,NULL,&dst);}
    }
    /* The presentation-layer focus frame is only wanted where the native
       cursor is hard to see: the settings menu and the two gallery
       interfaces (CG images and scene selection).  Every other screen keeps
       its own native cursor, so no frame is drawn there. */
    if(p->kind==8&&!p->config_motion&&p->setting_page<4&&p->selected<p->config_area_count[p->setting_page]){
        const ConfigArea *a=&p->config_areas[p->setting_page][p->selected];controls_focus(r,a->x0,a->y0,a->x1-a->x0,a->y1-a->y0);
    }
    else if(p->kind==22){int a[4];if(bootstrap_native_cg_focus(b,a))controls_focus(r,a[0],a[1],a[2],a[3]);}
    else if(p->kind==20&&!p->direct_motion){
        int f=p->direct_focus,x,y;unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);
        if(f>=0&&f<9){scene_mode_grid(p,b,(unsigned)f,&x,&y);controls_focus(r,x-5,y-4,160,120);}
        else if(f>=0x100&&f<0x200)controls_focus(r,0,172+(f-0x100)*28,112,20);
        else if(f==0x300)controls_focus(r,528,448,112,32);
        else if(f>=0x200&&f<0x300){unsigned pages=(scene_mode_items(b,cat)+8)/9;int hage=scene_mode_hage(b);controls_focus(r,(hage?460:498)+(f-0x200+(hage?4:3)-(int)pages)*38,0,48,20);}
    }
}

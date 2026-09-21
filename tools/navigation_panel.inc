/* Native 4fae70 scene selector.  The original uses two AKB layers and a
   600-slot catalog; keep the catalog visible even when its completion byte is
   zero so locked entries do not disappear from the menu. */
static void scene_mode_prepare(MessagePanel *p,KBootstrap *b){
    if(p->direct_count)return;
    if(!b->scene_history.loaded)khistory_load(&b->scene_history,bootstrap_save_dir(b));
    p->direct_count=600;p->direct_selected=0;p->direct_page=0;
    while(p->direct_selected<600&&!khistory_catalog(p->direct_selected,NULL,NULL)){
        KSceneCheckpoint cp;if(!khistory_checkpoint(&b->scene_history,p->direct_selected,&cp))break;
        p->direct_selected++;
    }
    if(p->direct_selected>=600)p->direct_selected=0;
}
/* 4a7d40/472700 create two different selectors.  Both use the same 600-slot
   SceneData catalog, but their native area files expose a 3x3 grid (nine
   entries per page), not the ten-line list used by the old preview. */
static int scene_mode_hage(const KBootstrap *b){
    return b&&b->vm&&b->vm->global_count[1]>61&&!b->vm->globals[1][61].string&&b->vm->globals[1][61].number;
}
static unsigned scene_mode_page_size(void){return 9;}
static int scene_mode_checkpoint(KBootstrap *b,unsigned slot,KSceneCheckpoint *cp){
    if(!b||!cp||slot>=600)return -1;
    if(!b->scene_history.loaded&&khistory_load(&b->scene_history,bootstrap_save_dir(b)))return -1;
    return khistory_checkpoint(&b->scene_history,slot,cp);
}
static void scene_mode_action(MessagePanel *p,KBootstrap *b,int action){
    scene_mode_prepare(p,b);
    if(action==1){
        b->scene_modal=0;b->scene_panel_request=-1;
        if(kvm_push(b->vm,(KValue){-1,NULL}))snprintf(p->status,sizeof(p->status),"返回值压栈失败");
        p->kind=0;p->direct_scene=0;return;
    }
    unsigned old=p->direct_selected;
    unsigned page_size=scene_mode_page_size(),col=p->direct_selected%3;
    if(action==2&&p->direct_selected>=3)p->direct_selected-=3;
    if(action==3&&p->direct_selected+3<600)p->direct_selected+=3;
    if(action==4&&col)p->direct_selected--;
    if(action==5&&col<2&&p->direct_selected+1<600)p->direct_selected++;
    if(action==8&&p->direct_selected>=page_size)p->direct_selected-=page_size;
    if(action==9&&p->direct_selected+page_size<600)p->direct_selected+=page_size;
    if(old!=p->direct_selected)p->direct_page=p->direct_selected/10;
    if(action)return;
    KSceneCheckpoint cp;
    if(scene_mode_checkpoint(b,p->direct_selected,&cp)){
        snprintf(p->status,sizeof(p->status),"该场景尚未解锁");return;
    }
    /* The script reads sys18 after the native modal returns.  The replay
       runner also receives the exact SceneData slot, so selection and playback
       use the same completion record. */
    if(b->vm->global_count[0]>18)b->vm->globals[0][18]=(KValue){(int32_t)cp.scene,NULL};
    if(kvm_push(b->vm,(KValue){(int32_t)cp.scene,NULL})){
        snprintf(p->status,sizeof(p->status),"返回值压栈失败");return;
    }
    b->scene_modal=0;b->scene_panel_request=0;
    p->scene_request=p->direct_selected+1;p->kind=16;p->direct_scene=0;
}
static int scene_mode_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    scene_mode_prepare(p,b);
    if(ui_canvas(&p->image)||ui_akb_asset(b,"kisaku_dl_seanemode_p.akb",&p->direct_artwork)||
       ui_akb_asset(b,"kisaku_dl_seanemode_p2.akb",&p->direct_parts))return -1;
    memset(p->image.pixels,0,p->image.stride*p->image.height);
    ui_blit(&p->image,&p->direct_artwork,0,0,p->direct_artwork.width,p->direct_artwork.height,72,0,1);
    ui_blit(&p->image,&p->direct_parts,0,0,p->direct_parts.width,p->direct_parts.height,160,92,1);
    int hage=scene_mode_hage(b);unsigned page=p->direct_selected/9,first=page*9;
    static const int normal_x[3]={133,301,469},normal_y[3]={40,176,312};
    static const int hage_x[3]={64,240,416},hage_y[3]={44,180,316};
    for(unsigned i=0;i<9&&first+i<600;i++){
        unsigned slot=first+i,scene=0,part=0;char line[96];
        if(khistory_catalog(slot,&scene,&part))continue;
        KSceneCheckpoint cp;int unlocked=!scene_mode_checkpoint(b,slot,&cp);
        int col=(int)(i%3),row=(int)(i/3),x=hage?hage_x[col]:normal_x[col],y=hage?hage_y[row]:normal_y[row];
        snprintf(line,sizeof(line),"%s%03u-%02u %s",slot==p->direct_selected?">":" ",scene,part,unlocked?"":"×");
        ui_text(b,&p->image,line,x+8,y+8,(hage?144:133),22,slot==p->direct_selected?0xffe276:unlocked?0xffffff:0x8c8c8c,0);
    }
    char footer[128];snprintf(footer,sizeof(footer),"%s回想场景  %u/%u    A:播放  B:返回",hage?"秃作 ":"",p->direct_selected+1,600);
    ui_fill(&p->image,0,448,640,32,0x201a12);ui_text(b,&p->image,footer,12,454,616,20,0xffffff,0);
    if(p->status[0])ui_text(b,&p->image,p->status,184,410,272,24,0xffb85c,0);
    ui_present(r,&p->image,&p->texture);return 0;
}
/* The PC selector accepts both the controller focus and the original
   rectangular hit areas.  Keep pointer/touch selection on the same catalog
   slot and action path as the D-pad so lock checks and replay preparation
   cannot diverge between input devices. */
static void scene_mode_pointer(MessagePanel *p,KBootstrap *b,int x,int y,int click){
    scene_mode_prepare(p,b);
    if(click==2){scene_mode_action(p,b,1);return;}
    int hage=scene_mode_hage(b);unsigned page=p->direct_selected/9;
    /* 472700/4a7d40 expose page tabs through AREA ids 0x100+.  Their
       geometry differs between the two selector classes; map them to the
       same nine-slot page cursor used by controller navigation. */
    if(click&&!hage&&x>=0&&x<112&&y>=172&&y<472){
        unsigned tab=(unsigned)(y-172)/28;if(tab<11){unsigned slot=tab*9;if(slot<600){p->direct_selected=slot;p->direct_page=tab;}return;}
    }
    if(click&&hage&&x>=460&&x<622&&y<24){
        unsigned tab=(unsigned)(x-460)/38;if(tab<4){unsigned slot=tab*9;if(slot<600){p->direct_selected=slot;p->direct_page=tab;}return;}
    }
    /* Both area files reserve (528,448)-(640,480) for cancel.  Keep the
       preview's left footer page step, but never let the native cancel zone
       become a page-forward hit. */
    if(click&&y>=438){if(x<128)scene_mode_action(p,b,8);else scene_mode_action(p,b,1);return;}
    static const int normal_x[3]={133,301,469},normal_y[3]={40,176,312};
    static const int hage_x[3]={64,240,416},hage_y[3]={44,180,316};
    const int *xs=hage?hage_x:normal_x,*ys=hage?hage_y:normal_y;int w=hage?160:149;
    for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++){
        if(x>=xs[col]&&x<xs[col]+w&&y>=ys[row]&&y<ys[row]+(hage?120:112)){
            unsigned slot=page*9+row*3+col;if(slot<600){p->direct_selected=slot;p->direct_page=page;if(click)scene_mode_action(p,b,0);}return;
        }
    }
    if(click&&y>=408&&y<438){scene_mode_action(p,b,x<320?8:9);return;}
}
/* AI6WIN 48d2c9 / 48d6c1: scene ID selects NNN.rmt; part n is the
   120x90 tile at ((n-1)*120,0). Four frames per filmstrip page. */
static void navigation_catalog(MessagePanel *p,KBootstrap *b){
    if(p->nav_catalog_ready)return;
    memset(p->nav_catalog,0,sizeof(p->nav_catalog));
    for(unsigned slot=1;slot<=100;slot++){
        KFlags *f=NULL;KControlStore *c=NULL;
        if(!kslot_read(bootstrap_save_dir(b),0,slot,&f,&c)&&c->count&&memchr(f->module,0,sizeof(f->module)))p->nav_catalog[slot-1]=kscene_module((char *)f->module);
        kflags_free(f);kcontrol_free(c);
    }p->nav_catalog_ready=1;
}
static int navigation_map_draw(MessagePanel *p,KBootstrap *b){
    if(bootstrap_scene_draw(b,p->selected,p->viewing,&p->image))return -1;
    navigation_catalog(p,b);KScene *s=b->scene;const KSceneNode *focus=&s->nodes[p->selected];
    int left=focus->x-290,top=focus->y-219;if(left<0)left=0;if(top<0)top=0;
    if(!p->viewing)for(unsigned i=0;i<s->count;i++){const KSceneNode *n=&s->nodes[i];if(!n->width)continue;
        unsigned slot=0;while(slot<100&&p->nav_catalog[slot]!=n->id)slot++;if(slot==100)continue;
        int x=n->x-left+44,y=n->y-top;if(x<0||x>=624||y<0||y>=464)continue;
        ui_fill(&p->image,x,y,16,16,0x261c09);ui_text(b,&p->image,"S",x+3,y,14,16,0xffdf68,0);
    }return 0;
}
static void navigation_panel_refresh(MessagePanel *p,KBootstrap *b){
    unsigned id=b->scene&&p->selected<b->scene->count?b->scene->nodes[p->selected].id:0;
    if(!id||p->nav_id==id)return;
    p->nav_id=id;p->nav_count=0;p->nav_page_key=0;p->variant=0;
    rmt_free(&p->nav_scene);char name[32];snprintf(name,sizeof(name),"%03u.rmt",id);ui_asset(b,name,&p->nav_scene);
    navigation_catalog(p,b);
    for(unsigned slot=1;slot<=100;slot++)if(p->nav_catalog[slot-1]==id)p->nav_slots[p->nav_count++]=slot;
    p->nav_mode=p->nav_count&&bootstrap_message_setting(b,22,0)==1;
}
static unsigned navigation_panel_count(MessagePanel *p){return p->nav_mode?p->nav_count:p->nav_scene.width/120;}
static unsigned navigation_checkpoint(MessagePanel *p,KBootstrap *b,unsigned part){
    unsigned slots[600],count=scene_replay_slots(p,b,slots);
    for(unsigned i=0;i<count;i++){KSceneCheckpoint cp;if(!khistory_checkpoint(&b->scene_history,slots[i],&cp)&&cp.part==part)return slots[i]+1;}
    return 0;
}
static void navigation_panel_action(MessagePanel *p,KBootstrap *b,int action){
    navigation_panel_refresh(p,b);
    if(action==1){p->kind=4;p->status[0]=0;return;}
    if(action==6){p->nav_mode^=1;p->variant=0;p->nav_page_key=0;p->status[0]=0;return;}
    unsigned count=navigation_panel_count(p);if(!count)return;
    if(action==2||action==4)p->variant=(p->variant+count-1)%count;
    if(action==3||action==5)p->variant=(p->variant+1)%count;
    if(action==8||action==9){unsigned pages=(count+3)/4,page=(p->variant/4+(action==8?pages-1:1))%pages;p->variant=page*4;}
    if(!action){if(p->nav_mode)p->nav_load_request=p->nav_slots[p->variant%count];
        else{p->scene_request=navigation_checkpoint(p,b,p->variant+1);if(!p->scene_request)snprintf(p->status,sizeof(p->status),"この場面はまだ再生できません");}}
}
static void navigation_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    navigation_panel_refresh(p,b);
    if(ui_canvas(&p->image)||ui_asset(b,"navi_p.rmt",&p->nav_artwork))return;
    /* Map is retained behind the original floating filmstrip. */
    if(navigation_map_draw(p,b))ui_fill(&p->image,0,0,640,480,0x18130d);
    unsigned count=navigation_panel_count(p),page=p->variant/4;
    ui_blit(&p->image,&p->nav_artwork,0,p->nav_mode?120:0,640,118,0,178,0);
    unsigned key=1+page+p->nav_mode*100;
    if(p->nav_page_key!=key){p->nav_page_key=key;for(unsigned i=0;i<4;i++){
        rmt_free(&p->nav_previews[i]);p->nav_dates[i]=0;
        if(p->nav_mode&&page*4+i<count)kslot_preview(bootstrap_save_dir(b),0,p->nav_slots[page*4+i],&p->nav_previews[i],&p->nav_dates[i]);
    }}
    for(unsigned i=0;i<4;i++){
        unsigned item=page*4+i;int x=4+(int)i*128;if(item>=count)continue;
        if(p->nav_mode){ui_scale(&p->image,&p->nav_previews[i],x,192,120,90);}
        else if(navigation_checkpoint(p,b,item+1))ui_blit(&p->image,&p->nav_scene,(int)item*120,0,120,90,x,192,0);
        if(item==p->variant){ui_fill(&p->image,x-2,190,124,2,0xffe37b);ui_fill(&p->image,x-2,282,124,2,0xffe37b);}
        char label[48];snprintf(label,sizeof(label),p->nav_mode?"SAVE %03u":"SCENE %u",p->nav_mode?p->nav_slots[item]:item+1);ui_text(b,&p->image,label,x,306,124,20,0xffe37b,0);
        if(p->nav_mode&&p->nav_dates[i]){time_t stamp=(time_t)p->nav_dates[i];struct tm *tm=localtime(&stamp);if(tm){strftime(label,sizeof(label),"%m/%d %H:%M",tm);ui_text(b,&p->image,label,x,330,124,20,0xffffff,0);}}
    }
    if(!count)ui_text(b,&p->image,p->nav_mode?"この場面のセーブはありません":"再生できる場面がありません",24,222,480,32,0xffffff,0);
    ui_text(b,&p->image,"左右: 選択  L/R: ページ  Y: SCENE/LOAD  A: 決定  B: 戻る",24,408,604,44,0xffffff,0);
    if(p->status[0])ui_text(b,&p->image,p->status,24,368,604,32,0xffdf74,0);
    ui_present(r,&p->image,&p->texture);
}

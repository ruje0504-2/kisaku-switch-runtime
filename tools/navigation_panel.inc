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

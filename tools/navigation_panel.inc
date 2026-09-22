#include "../runtime/scene_mode_catalog.h"
/* Native 4fae70 selector artwork. Completion/playback still uses the
   portable checkpoint bridge pending the native scene-script dispatch. */
static int scene_mode_hage(const KBootstrap *b);
static void scene_mode_prepare(MessagePanel *p,KBootstrap *b){
    if(p->direct_count)return;
    if(!b->scene_history.loaded)khistory_load(&b->scene_history,bootstrap_save_dir(b));
    p->direct_count=scene_mode_hage(b)?30:kscene_mode_total();p->direct_selected=0;p->direct_page=0;
}
static int scene_mode_hage(const KBootstrap *b){
    return b&&b->vm&&b->vm->global_count[1]>61&&!b->vm->globals[1][61].string&&b->vm->globals[1][61].number;
}
static int scene_mode_checkpoint(KBootstrap *b,unsigned slot,KSceneCheckpoint *cp){
    if(!b||!cp||slot>=600)return -1;
    if(!b->scene_history.loaded&&khistory_load(&b->scene_history,bootstrap_save_dir(b)))return -1;
    return khistory_checkpoint(&b->scene_history,slot,cp);
}
static unsigned scene_mode_items(KBootstrap *b,unsigned cat){return scene_mode_hage(b)?30:kscene_mode_counts[cat];}
static void scene_mode_at(KBootstrap *b,unsigned flat,unsigned *cat,unsigned *item){
    if(scene_mode_hage(b)){*cat=0;*item=flat;}else if(kscene_mode_at(flat,cat,item)){*cat=0;*item=0;}
}
static unsigned scene_mode_base(unsigned cat){unsigned n=0;for(unsigned c=0;c<cat;c++)n+=kscene_mode_counts[c];return n;}
static void scene_mode_action(MessagePanel *p,KBootstrap *b,int action){
    scene_mode_prepare(p,b);if(action==1){b->scene_modal=0;b->scene_panel_request=-1;if(kvm_push(b->vm,(KValue){-1,NULL}))snprintf(p->status,sizeof(p->status),"返回值压栈失败");p->kind=0;p->direct_scene=0;return;}
    unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);unsigned old=p->direct_selected;
    if(action==2&&item>=3)p->direct_selected-=3; else if(action==3&&item+3<scene_mode_items(b,cat))p->direct_selected+=3;
    else if(action==4&&item%3)p->direct_selected--; else if(action==5&&item%3<2&&item+1<scene_mode_items(b,cat))p->direct_selected++;
    else if(action==8&&item>=9)p->direct_selected-=9; else if(action==9&&item+9<scene_mode_items(b,cat))p->direct_selected+=9;
    if(old!=p->direct_selected){unsigned nc,ni;scene_mode_at(b,p->direct_selected,&nc,&ni);p->direct_page=ni/9;}
    if(action)return;
    KSceneCheckpoint cp;if(scene_mode_checkpoint(b,p->direct_selected,&cp)){snprintf(p->status,sizeof(p->status),"该场景尚未解锁");return;}
    if(b->vm->global_count[0]>18)b->vm->globals[0][18]=(KValue){(int32_t)cp.scene,NULL};
    if(kvm_push(b->vm,(KValue){(int32_t)cp.scene,NULL})){snprintf(p->status,sizeof(p->status),"返回值压栈失败");return;}b->scene_modal=0;b->scene_panel_request=0;p->scene_request=p->direct_selected+1;p->kind=16;p->direct_scene=0;
}
static int scene_mode_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    scene_mode_prepare(p,b);
    if(ui_canvas(&p->image)||ui_akb_asset(b,"kisaku_dl_seanemode_p.akb",&p->direct_artwork)||
       ui_akb_asset(b,"kisaku_dl_seanemode_p2.akb",&p->direct_parts))return -1;
    memset(p->image.pixels,0,p->image.stride*p->image.height);
    int hage=scene_mode_hage(b);unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);
    unsigned page=item/9; char asset[64];
    if(hage){unsigned lo=page*9,hi=lo+8;if(page==3)hi=29;snprintf(asset,sizeof(asset),"hage_scene_%02u_%02u.akb",lo,hi);}else snprintf(asset,sizeof(asset),"scene_%02u_%02u.akb",kscene_mode_resource_group[cat],page);
    if(strcmp(asset,p->direct_thumb_name)){rmt_free(&p->direct_thumb);p->direct_thumb_name[0]=0;ui_akb_asset(b,asset,&p->direct_thumb);if(p->direct_thumb.pixels)snprintf(p->direct_thumb_name,sizeof(p->direct_thumb_name),"%s",asset);}
    /* p and p2 are atlases.  Drawing either atlas wholesale produces the
       duplicated ELF/label blocks visible in the old preview.  471950 only
       copies the individual frame, category, tab and return rectangles. */
    static const int normal_x[3]={133,301,469},normal_y[3]={40,176,312};
    static const int hage_x[3]={64,240,416},hage_y[3]={44,180,316};
    for(unsigned i=0;i<9;i++){
        unsigned flat=0,base=0;for(unsigned c=0;c<11;c++){if(c==cat){flat=base+page*9+i;break;}base+=kscene_mode_counts[c];}
        if(page*9+i>=scene_mode_items(b,cat)||flat>=p->direct_count)continue;
        int col=(int)(i%3),row=(int)(i/3),x=hage?hage_x[col]:normal_x[col],y=hage?hage_y[row]:normal_y[row];
        KSceneCheckpoint cp;int unlocked=!scene_mode_checkpoint(b,flat,&cp);
        if(unlocked&&p->direct_thumb.pixels){
            /* 471740: columns are variants, rows are scene slots. */
            ui_blit(&p->image,&p->direct_thumb,0,(int)i*112,149,112,x,y,1);
        }
        /* unlocked/selected frames are rows 28/148 in p; the locked row
           268 is the opaque cover used by CScMode for incomplete entries. */
        unsigned frame_y=unlocked&&flat==p->direct_selected?148u:28u;
        ui_blit(&p->image,&p->direct_artwork,224,(int)frame_y,160,120,x-5,y-4,1);
        if(!unlocked)ui_blit(&p->image,&p->direct_artwork,224,268,149,112,x,y,1);
    }
    /* Category labels, page tabs and cancel are all sprite rectangles from
       the same two atlases.  The resource-group table is the 0x547c74 map
       used by 471950; it is independent of the flat scene slot number. */
    if(!hage)for(unsigned c=0;c<11;c++){
        unsigned map=kscene_mode_resource_group[c],state=c==cat?2u:0u;
        ui_blit(&p->image,&p->direct_artwork,(int)((map/6)*112),(int)((state+(map%6)*4)*20),112,20,0,172+(int)c*28,1);
    }
    unsigned pages=(scene_mode_items(b,cat)+8)/9;if(pages>4)pages=4;
    for(unsigned i=0;i<pages;i++){
        unsigned state=i==page?2u:0u;
        ui_blit(&p->image,&p->direct_artwork,224+(int)i*48,380+(int)state*20,48,20,(hage?460:498)+(int)(i+((hage?4u:3u)-pages))*38,0,1);
    }
    ui_blit(&p->image,&p->direct_artwork,368,380,28,20,612,0,1);
    ui_blit(&p->image,&p->direct_artwork,384,0,112,32,528,448,1);
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
    int hage=scene_mode_hage(b);unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);unsigned page=item/9,base=scene_mode_base(cat);
    /* 472700/4a7d40 expose page tabs through AREA ids 0x100+.  Their
       geometry differs between the two selector classes; map them to the
       same nine-slot page cursor used by controller navigation. */
    if(click&&!hage&&x>=0&&x<112&&y>=172&&y<472){
        unsigned tab=(unsigned)(y-172)/28;if(tab<11){unsigned slot=scene_mode_base(tab);if(slot<p->direct_count){p->direct_selected=slot;p->direct_page=0;}return;}
    }
    if(click&&hage&&x>=460&&x<622&&y<24){
        unsigned tab=(unsigned)(x-460)/38;if(tab<4){unsigned slot=tab*9;if(slot<p->direct_count){p->direct_selected=slot;p->direct_page=0;}return;}
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
            unsigned slot=base+page*9+row*3+col;if(page*9+row*3+col<scene_mode_items(b,cat)&&slot<p->direct_count){p->direct_selected=slot;p->direct_page=page;if(click)scene_mode_action(p,b,0);}return;
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

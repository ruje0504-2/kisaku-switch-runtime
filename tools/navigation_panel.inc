#include "../runtime/scene_mode.h"
/* 4fae70 owns the modal; the original MES resumes playback after its return.
   Do not route this selector through the unrelated checkpoint history menu. */
static int scene_mode_hage(const KBootstrap *b){return b&&b->vm&&b->vm->global_count[1]>61&&!b->vm->globals[1][61].string&&b->vm->globals[1][61].number==1;}
static unsigned scene_mode_base(unsigned cat){unsigned n=0;for(unsigned c=0;c<cat;c++)n+=kscene_mode_counts[c];return n;}
static unsigned scene_mode_items(KBootstrap *b,unsigned cat){return scene_mode_hage(b)?30:kscene_mode_counts[cat];}
static void scene_mode_at(KBootstrap *b,unsigned flat,unsigned *cat,unsigned *item){if(scene_mode_hage(b)){*cat=0;*item=flat;}else if(kscene_mode_at(flat,cat,item)){*cat=0;*item=0;}}
static int scene_mode_bank(KBootstrap *b,unsigned index){KValue v=b->vm->globals[1][index];return v.string?0:v.number;}
static int scene_mode_finish_now(MessagePanel *p,KBootstrap *b,int result);
static int scene_mode_copy(KImage *dst,const KImage *src){
    if(ui_canvas(dst))return -1;
    ui_fill(dst,0,0,640,480,0);if(src&&src->pixels)ui_blit(dst,src,0,0,640,480,0,0,0);return 0;
}
/* 46bbe0(16): 8/4/0 steps, or 16 when flag4000 is set. */
static unsigned scene_mode_steps(KBootstrap *b){int speed=bootstrap_message_setting(b,8,0);return b->vm->globals[0][50].number&0x4000?16:speed==2?0:speed==1?4:8;}
static int scene_mode_motion(MessagePanel *p,KBootstrap *b,unsigned motion){
    if(scene_mode_copy(&p->direct_from,&p->image)){snprintf(p->status,sizeof(p->status),"场景动画缓冲分配失败");return -1;}
    p->direct_motion=motion;p->direct_steps=motion==4?1:scene_mode_steps(b);p->direct_alpha=0;p->direct_clock=SDL_GetTicks64();p->direct_hover=-1;
    return 0;
}
static void scene_mode_tick(MessagePanel *p,KBootstrap *b,uint64_t now){
    if(!p->direct_motion)return;
    uint64_t elapsed=now>=p->direct_clock?now-p->direct_clock:0;
    unsigned step=(unsigned)(elapsed/15);
    p->direct_alpha=p->direct_steps&&step<p->direct_steps?step*255/p->direct_steps:255;
    if(p->direct_alpha==255){unsigned motion=p->direct_motion;p->direct_motion=0;p->direct_hover=-1;p->direct_hover_clock=now;if(motion==3||motion==4)scene_mode_finish_now(p,b,p->direct_result);}
}
static void scene_mode_skip(MessagePanel *p,KBootstrap *b){
    if(p->direct_motion&&!(b->vm->globals[0][50].number&0x4000))scene_mode_tick(p,b,p->direct_clock+(uint64_t)p->direct_steps*15);
}
static int scene_mode_finish(MessagePanel *p,KBootstrap *b,int result){
    if(!scene_mode_hage(b)&&result<0)return scene_mode_finish_now(p,b,result);
    p->direct_result=result;
    if(scene_mode_motion(p,b,scene_mode_hage(b)?3:4))return -1;
    return 0;
}
static void scene_mode_prepare(MessagePanel *p,KBootstrap *b){
    if(p->direct_count)return;
    int hage=scene_mode_hage(b),cat=scene_mode_bank(b,12),page=scene_mode_bank(b,13),slot=scene_mode_bank(b,14),variant=scene_mode_bank(b,15);
    p->direct_count=hage?30:kscene_mode_total();p->direct_sub=-1;p->direct_focus=0;
    p->direct_thumb_name[0]=0;p->direct_hover=-1;
    if(scene_mode_copy(&p->image,&b->layers[0])||scene_mode_copy(&p->direct_background,hage?NULL:&b->layers[3])){p->direct_count=0;snprintf(p->status,sizeof(p->status),"场景背景分配失败");return;}
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){uint8_t *q=p->direct_background.pixels+y*p->direct_background.stride+x*4;for(unsigned c=0;c<3;c++)q[c]=(uint8_t)(q[c]*127/255);}
    if(scene_mode_motion(p,b,1)){p->direct_count=0;return;}
    if(hage){page=cat>=0&&cat<4?cat:0;cat=0;slot=0;}
    else {if(cat<0||cat>=11)cat=0;if(page<0||page>=(kscene_mode_counts[cat]+8)/9)page=0;slot=slot<0?0:slot%9;}
    if(page*9+slot>=(int)scene_mode_items(b,(unsigned)cat))slot=0;
    p->direct_selected=(hage?0:scene_mode_base((unsigned)cat))+(unsigned)(page*9+slot);p->direct_page=(unsigned)page;
    if(!hage&&variant>=0&&(unsigned)variant<kscene_mode_variants((unsigned)cat,(unsigned)(page*9+slot))){p->direct_sub=variant;p->direct_focus=variant;}
}
static unsigned scene_mode_visible(MessagePanel *p,KBootstrap *b){unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);if(p->direct_sub>=0)return kscene_mode_variants(cat,item);unsigned n=scene_mode_items(b,cat)-item/9*9;return n<9?n:9;}
static int scene_mode_unlocked(MessagePanel *p,KBootstrap *b,unsigned slot){
    unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);
    if(slot>=scene_mode_visible(p,b))return 0;
    if(scene_mode_hage(b))return kscene_mode_flag_value(b->vm->bytes,b->vm->byte_count,kscene_mode_hage_flags[item/9*9+slot])==1;
    return kscene_mode_unlocked(b->vm->bytes,b->vm->byte_count,cat,p->direct_sub>=0?item:item/9*9+slot,p->direct_sub>=0?(int)slot:-1);
}
static void scene_mode_page(MessagePanel *p,KBootstrap *b,unsigned cat,unsigned page){
    if(scene_mode_motion(p,b,2))return;
    p->direct_selected=(scene_mode_hage(b)?0:scene_mode_base(cat))+page*9;p->direct_page=page;p->direct_sub=-1;p->status[0]=0;
    if(scene_mode_hage(b))b->vm->globals[1][12]=(KValue){(int32_t)page,NULL};
}
static int scene_mode_finish_now(MessagePanel *p,KBootstrap *b,int result){
    if(kvm_push(b->vm,(KValue){result,NULL})){snprintf(p->status,sizeof(p->status),"返回值压栈失败");return -1;}
    /* Commit selector parameters only once the delayed return can be pushed. */
    if(result>=0&&!scene_mode_hage(b)){
        unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);
        b->vm->globals[1][12]=(KValue){(int32_t)cat,NULL};b->vm->globals[1][13]=(KValue){(int32_t)(item/9),NULL};
        b->vm->globals[1][14]=(KValue){(int32_t)item,NULL};b->vm->globals[1][15]=(KValue){p->direct_sub>=0?p->direct_focus:-1,NULL};
    }
    b->vm->bytes[4012]=0;b->scene_modal=0;b->scene_panel_request=0;p->kind=0;p->direct_scene=0;return 0;
}
static void scene_mode_action(MessagePanel *p,KBootstrap *b,int action){
    scene_mode_prepare(p,b);scene_mode_tick(p,b,SDL_GetTicks64());if(p->direct_motion||p->kind!=20||!p->direct_count)return;unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);
    unsigned page=item/9,n=scene_mode_visible(p,b),pages=(scene_mode_items(b,cat)+8)/9;int f=p->direct_focus,hage=scene_mode_hage(b);
    if(action==1){if(p->direct_sub>=0){if(scene_mode_motion(p,b,2))return;p->direct_sub=-1;p->direct_focus=(int)(item%9);p->status[0]=0;}else scene_mode_finish(p,b,-1);return;}
    if((action==8||action==9)&&p->direct_sub<0){unsigned next=action==8?(page? page-1:page):(page+1<pages?page+1:page);scene_mode_page(p,b,cat,next);p->direct_focus=0;return;}
    if(action>=2&&action<=5){
        if(f<0)f=0;
        else if(f<0x100){
            if(action==2)f=f>=3?f-3:(p->direct_sub<0?0x200:f);
            else if(action==3)f=f+3<(int)n?f+3:0x300;
            else if(action==4)f=f%3?f-1:(!hage&&p->direct_sub<0?0x100+(f==0?0:f==3?3:8):f);
            else if(f%3<2&&f+1<(int)n)f++;
        }else if(f<0x200){int c=f&255;if(action==2&&c>0)f--;if(action==3&&c<10)f++;if(action==5){f=c==10?0x300:c==0?0:c<5?3:6;if(f<0x100&&f>=(int)n)f=(int)(n-1);}}
        else if(f<0x300){if(action==3)f=0;if(action==4&&f>0x200)f--;if(action==5&&(unsigned)(f-0x200+1)<pages)f++;}
        else {if(action==2)f=(int)n-1;if(action==4&&!hage&&p->direct_sub<0)f=0x10a;}
        p->direct_focus=f;p->status[0]=0;return;
    }
    if(action||f<0)return;
    if(f==0x300){scene_mode_action(p,b,1);return;}
    if(f>=0x200){if(p->direct_sub<0&&(unsigned)(f&255)<pages)scene_mode_page(p,b,cat,(unsigned)(f&255));return;}
    if(f>=0x100){unsigned c=(unsigned)(f&255);if(!hage&&p->direct_sub<0&&c<11&&(c!=10||b->vm->bytes[4006]))scene_mode_page(p,b,c,0);return;}
    if(!scene_mode_unlocked(p,b,(unsigned)f)){snprintf(p->status,sizeof(p->status),"该场景尚未解锁");return;}
    if(!hage&&p->direct_sub<0){item=page*9+(unsigned)f;p->direct_selected=scene_mode_base(cat)+item;if(kscene_mode_variants(cat,item)){if(scene_mode_motion(p,b,2))return;p->direct_sub=0;p->direct_focus=0;p->status[0]=0;return;}}
    /* 4735ca..473655 stores category, page, page*9+slot, variant. */
    scene_mode_finish(p,b,hage?(int)(page*256)+(f&255):0);
}
static void scene_mode_grid(MessagePanel *p,KBootstrap *b,unsigned slot,int *x,int *y){
    if(p->direct_sub>=0){unsigned n=scene_mode_visible(p,b),cols=n<3?n:3;*x=(640-(int)cols*168)/2+5+(int)(slot%3)*168;*y=(480-(int)((n+2)/3)*136)/2+4+(int)(slot/3)*136;}
    else if(scene_mode_hage(b)){*x=69+(int)(slot%3)*176;*y=48+(int)(slot/3)*136;}
    else {*x=133+(int)(slot%3)*168;*y=40+(int)(slot/3)*136;}
}
#include "scene_mode_thumbnails.inc"
/* Both animation clocks are independent of renderer frame rate. CScMode's
   IMsgTask constructor (4050b0) supplies 15 ms ticks. */
static void scene_mode_cycle(int hage,unsigned frames,uint64_t elapsed,unsigned *from,unsigned *to,unsigned *alpha){
    uint64_t tick=elapsed/15,cycle=hage?92:131;unsigned fade=hage?40:80;
    *from=frames?(unsigned)((tick/cycle)%frames):0;*to=frames?(*from+1)%frames:0;
    unsigned phase=(unsigned)(tick%cycle);*alpha=phase<fade?phase*255/fade:255;
}
static void scene_mode_mix(KImage *d,const KImage *a,unsigned alpha){
    if(!a->pixels)return;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *q=d->pixels+y*d->stride+x*4;const uint8_t *p=a->pixels+y*a->stride+x*4;
        for(unsigned c=0;c<3;c++)q[c]=(uint8_t)(q[c]*alpha/255+p[c]*(255-alpha)/255);
        q[3]=255;
    }
}
static int scene_mode_draw_at(MessagePanel *p,KBootstrap *b,SDL_Renderer *r,uint64_t now){
    scene_mode_prepare(p,b);scene_mode_tick(p,b,now);if(p->kind!=20||!p->direct_count)return 0;
    if(ui_canvas(&p->image)||ui_akb_asset(b,"kisaku_dl_seanemode_p.akb",&p->direct_artwork)||ui_akb_asset(b,"kisaku_dl_cgmode_p.akb",&p->direct_parts))return -1;
    ui_blit(&p->image,&p->direct_background,0,0,640,480,0,0,0);
    int hage=scene_mode_hage(b);unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);unsigned page=item/9;char asset[64];
    if(hage){unsigned lo=page*9,hi=lo+8;if(hi>29)hi=29;snprintf(asset,sizeof(asset),"hage_scene_%02u_%02u.akb",lo,hi);}
    else if(p->direct_sub>=0)snprintf(asset,sizeof(asset),"scene_%02u_%02u_%02u.akb",kscene_mode_resource_group[cat],page,item%9);
    else snprintf(asset,sizeof(asset),"scene_%02u_%02u.akb",kscene_mode_resource_group[cat],page);
    if(strcmp(asset,p->direct_thumb_name)){
        rmt_free(&p->direct_thumb);p->direct_thumb_name[0]=0;if(ui_akb_asset(b,asset,&p->direct_thumb))return -1;
        if(!hage&&p->direct_sub<0&&scene_thumb_reorder(p,b,cat,page))return -1;
        memset(p->direct_frames,0,sizeof(p->direct_frames));memset(p->direct_unlocked,0,sizeof(p->direct_unlocked));
        for(unsigned i=0;i<scene_mode_visible(p,b);i++){
            if(scene_mode_unlocked(p,b,i))p->direct_frames[i]=hage?kscene_mode_hage_frames[page*9+i]:scene_thumb_count(&p->direct_thumb,i);
            if(!hage&&p->direct_sub<0)for(unsigned v=0;v<kscene_mode_variants(cat,page*9+i);v++)p->direct_unlocked[i]+=kscene_mode_unlocked(b->vm->bytes,b->vm->byte_count,cat,page*9+i,(int)v);
        }
        for(unsigned i=0;i<scene_mode_visible(p,b);i++){
            if(p->direct_frames[i]&&(p->direct_thumb.width<p->direct_frames[i]*149||p->direct_thumb.height<(i+1)*112)){
                snprintf(p->status,sizeof(p->status),"回想缩略图尺寸不足");return -1;
            }
        }
        p->direct_thumb_serial++;p->direct_hover=-1;snprintf(p->direct_thumb_name,sizeof(p->direct_thumb_name),"%s",asset);
    }
    if(p->direct_motion)p->direct_hover=-1;
    else if(p->direct_hover!=p->direct_focus){p->direct_hover=p->direct_focus;p->direct_hover_clock=now;}
    ui_blit(&p->image,&p->direct_artwork,224,0,116,28,hage?2:0,hage?2:0,1);
    for(unsigned i=0;i<scene_mode_visible(p,b);i++){
        int x,y;scene_mode_grid(p,b,i,&x,&y);int unlocked=scene_mode_unlocked(p,b,i);
        if(unlocked){
            unsigned from=0,to=0,alpha=0;
            if(!p->direct_motion&&p->direct_hover==(int)i&&p->direct_frames[i]>1)scene_mode_cycle(hage,p->direct_frames[i],now>=p->direct_hover_clock?now-p->direct_hover_clock:0,&from,&to,&alpha);
            ui_blit(&p->image,&p->direct_thumb,(int)from*149,(int)i*112,149,112,x,y,0);
            if(alpha)for(unsigned yy=0;yy<112;yy++)for(unsigned xx=0;xx<149;xx++){
                const uint8_t *src=p->direct_thumb.pixels+(i*112+yy)*p->direct_thumb.stride+(to*149+xx)*4;
                uint8_t *dst=p->image.pixels+(y+(int)yy)*p->image.stride+(x+(int)xx)*4;
                for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*alpha/255+dst[c]*(255-alpha)/255);
            }
            if(!hage&&p->direct_sub<0){unsigned total=kscene_mode_variants(cat,page*9+i);if(total>1){
                ui_blit(&p->image,&p->direct_parts,124+(int)p->direct_unlocked[i]*8,0,8,12,x+125,y+100,1);
                ui_blit(&p->image,&p->direct_parts,116,0,8,12,x+133,y+100,1);
                ui_blit(&p->image,&p->direct_parts,124+(int)total*8,0,8,12,x+141,y+100,1);
            }}
        }
        ui_blit(&p->image,&p->direct_artwork,224,unlocked&&p->direct_focus==(int)i?148:28,160,120,x-5,y-4,1);
        if(!unlocked)ui_blit(&p->image,&p->direct_artwork,224,268,149,112,x,y,1);
    }
    if(p->direct_sub<0){
        if(!hage)for(unsigned c=0;c<11;c++){
            unsigned map=kscene_mode_resource_group[c],state=c==10&&!b->vm->bytes[4006]?3:c==cat?2:p->direct_focus==(int)(0x100+c)?1:0;
            ui_blit(&p->image,&p->direct_artwork,(int)((map/6)*112),(int)((state+(map%6)*4)*20),112,20,0,172+(int)c*28,1);
        }
        unsigned pages=(scene_mode_items(b,cat)+8)/9;
        for(unsigned i=0;i<pages;i++){
            unsigned state=i==page?2:p->direct_focus==(int)(0x200+i)?1:0;
            const KImage *atlas=hage?&b->layers[8]:&p->direct_artwork;
            ui_blit(&p->image,atlas,(hage?0:224)+(int)i*48,(hage?184:380)+(int)state*20,48,20,(hage?460:498)+(int)(i+((hage?4:3)-pages))*38,0,1);
        }
        ui_blit(&p->image,hage?&b->layers[8]:&p->direct_artwork,hage?480:368,hage?184:380,28,20,612,0,1);
    }
    ui_blit(&p->image,&p->direct_artwork,384,p->direct_focus==0x300?32:0,112,32,528,448,1);
    if(p->status[0])ui_text(b,&p->image,p->status,184,450,328,24,0xffb85c,0);
    if(p->direct_motion==1||p->direct_motion==2)scene_mode_mix(&p->image,&p->direct_from,p->direct_alpha);
    else if(p->direct_motion==3||p->direct_motion==4){
        if(p->direct_result>=0&&p->direct_focus>=0&&p->direct_focus<9){int x,y;scene_mode_grid(p,b,(unsigned)p->direct_focus,&x,&y);
            for(int yy=y;yy<y+112;yy++)for(int xx=x;xx<x+149;xx++){uint8_t *q=p->image.pixels+yy*p->image.stride+xx*4;for(unsigned c=0;c<3;c++)q[c]=(uint8_t)(128+q[c]*127/255);}}
        if(p->direct_motion==3)for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){uint8_t *q=p->image.pixels+y*p->image.stride+x*4;for(unsigned c=0;c<3;c++)q[c]=(uint8_t)(q[c]*(255-p->direct_alpha)/255);}
    }
    ui_present(r,&p->image,&p->texture);return 0;
}
static int scene_mode_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){return scene_mode_draw_at(p,b,r,SDL_GetTicks64());}
static void scene_mode_pointer(MessagePanel *p,KBootstrap *b,int x,int y,int click){
    scene_mode_prepare(p,b);scene_mode_tick(p,b,SDL_GetTicks64());if(p->direct_motion||p->kind!=20)return;if(click==2){scene_mode_action(p,b,1);return;}
    int hage=scene_mode_hage(b),focus=-1;unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);
    if(x>=528&&x<640&&y>=448&&y<480)focus=0x300;
    if(p->direct_sub<0){
        if(!hage&&x>=0&&x<112&&y>=172&&y<472&&(y-172)%28<20)focus=0x100+(y-172)/28;
        unsigned pages=(scene_mode_items(b,cat)+8)/9;int start=(hage?460:498)+(int)((hage?4:3)-pages)*38;
        if(y>=0&&y<20&&x>=start&&x<start+(int)pages*38)focus=0x200+(x-start)/38;
    }
    for(unsigned i=0;i<scene_mode_visible(p,b);i++){int gx,gy;scene_mode_grid(p,b,i,&gx,&gy);if(x>=gx&&x<gx+149&&y>=gy&&y<gy+112){focus=(int)i;break;}}
    p->direct_focus=focus;if(click&&focus>=0)scene_mode_action(p,b,0);
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

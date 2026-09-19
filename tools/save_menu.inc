#include <time.h>
/* Original slot-page layout; all new files remain in the port namespace. */
typedef struct {
    int active,save,slot,selector,overwrite,loading;unsigned ticks;
    char status[100],labels[10][64],comments[10][43],note[129];KBootstrap *next;
    int page_key,focus,button,request;unsigned valid[10];int64_t dates[10];
    uint64_t draw_key;KImage artwork,canvas,previews[10];SDL_Texture *texture;
} SaveMenu;
static void save_menu_refresh(SaveMenu *m,const char *root){
    unsigned first=(unsigned)(m->slot-1)/10*10+1;
    int key=m->selector*10+(m->slot-1)/10+1;if(m->page_key==key)return;m->page_key=key;m->draw_key=0;
    for(unsigned i=0;i<10;i++){
        rmt_free(&m->previews[i]);m->dates[i]=0;m->valid[i]=0;m->comments[i][0]=0;
        KFlags *f=NULL;KControlStore *c=NULL;
        int bad=kslot_read(root,(unsigned)m->selector,first+i,&f,&c);
        if(!bad&&c->count&&memchr(f->module,0,260)&&f->module[0]){
            m->valid[i]=1;snprintf(m->labels[i],64,"%03u  %.40s",first+i,(char *)f->module);
            kslot_preview(root,(unsigned)m->selector,first+i,&m->previews[i],&m->dates[i]);
            KSlotInfo info;if(!kslot_info(f,first+i,&info)){memcpy(m->comments[i],info.comment,sizeof(info.comment));if(info.saved_time)m->dates[i]=info.saved_time;}
        }else snprintf(m->labels[i],64,"%03u",first+i);
        kflags_free(f);kcontrol_free(c);
    }
}
static void save_menu_open(SaveMenu *m,KBootstrap *b,int load){
    if(m->active||b->scene_replay)return;
    m->active=1;m->save=!load&&bootstrap_can_save(b);m->slot=1;
    m->selector=b->vm->bytes[8100]>3?3:b->vm->bytes[8100];m->overwrite=0;m->status[0]=0;m->focus=0;m->page_key=0;
    save_menu_refresh(m,bootstrap_save_dir(b));
}
/* action: 0 confirm, 1 back, 2 up, 3 down, 4 previous page, 5 next page,
   6 toggle save/load, 7 select next native progress group. */
static void save_menu_action(SaveMenu *m,KBootstrap *b,int action){
    if(m->loading){if(action==1){bootstrap_destroy(m->next);m->next=NULL;m->loading=0;snprintf(m->status,sizeof(m->status),"Load cancelled");}return;}
    if(action==1&&m->overwrite){SDL_StopTextInput();m->overwrite=0;m->status[0]=0;return;}
    if(action==1){m->active=0;m->overwrite=0;b->load_modal=0;bootstrap_message_hide(b,0);return;}
    if(m->overwrite&&action){
        if(action==7)m->note[0]=0;
#ifdef __SWITCH__
        if(action==6){SwkbdConfig keyboard;char note[129]={0};if(R_SUCCEEDED(swkbdCreate(&keyboard,0))){
            swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetInitialText(&keyboard,m->note);swkbdConfigSetStringLenMax(&keyboard,16);swkbdConfigSetStringLenMin(&keyboard,0);
            if(R_SUCCEEDED(swkbdShow(&keyboard,note,sizeof(note))))snprintf(m->note,sizeof(m->note),"%s",note);
            swkbdClose(&keyboard);
        }}
#endif
        return;
    }
    if(action==4||action==5){m->focus=action==4;m->overwrite=0;return;}
    if(m->focus&&(action==2||action==3)){m->button=(m->button+(action==2?5:1))%6;return;}
    if(m->focus&&!action){
        if(m->button<2){m->save=m->button==0&&bootstrap_can_save(b);m->focus=0;m->overwrite=0;return;}
        m->request=m->button==2?5:m->button==3?8:m->button==4?18:6;return;
    }
    if(action==2||action==3||action==8||action==9){int delta=action==2?-1:action==3?1:action==8?-10:10;m->slot=(m->slot-1+delta+100)%100+1;m->overwrite=0;m->status[0]=0;save_menu_refresh(m,bootstrap_save_dir(b));return;}
    if(action==6){m->focus=0;m->save=!m->save&&bootstrap_can_save(b);m->selector=b->vm->bytes[8100]>3?3:b->vm->bytes[8100];m->overwrite=0;m->status[0]=0;save_menu_refresh(m,bootstrap_save_dir(b));return;}
    if(action==7&&!m->save){m->focus=0;m->selector=(m->selector+1)%4;m->overwrite=0;m->status[0]=0;save_menu_refresh(m,bootstrap_save_dir(b));return;}
    if(action)return;
    if(m->save){
        if(!bootstrap_can_save(b)){snprintf(m->status,sizeof(m->status),"Save requires a settled dialogue checkpoint");return;}
        if(!m->overwrite){m->overwrite=1;ui_utf8_note(m->comments[(m->slot-1)%10],m->note);m->status[0]=0;SDL_StartTextInput();return;}
        uint8_t encoded[33];size_t length;
        if(m->note[0]&&ktext_name_encode(m->note,encoded,&length)){snprintf(m->status,sizeof(m->status),"日本語・英数字 16 文字以内");return;}
        int failed=bootstrap_save_slot_comment(b,(unsigned)m->slot,m->note);m->overwrite=0;SDL_StopTextInput();
        /* 失败时把存档根一并显示：NSP 下应是 save:，回退时是 SD 路径，
           落在 romfs 就说明存档根接错了。 */
        if(failed){
            snprintf(m->status,sizeof(m->status),"保存に失敗しました（旧データを保持）[%.24s]",bootstrap_save_dir(b));
        }else snprintf(m->status,sizeof(m->status),"保存しました");
        m->page_key=0;save_menu_refresh(m,bootstrap_save_dir(b));
    }else{
        KFlags *f=NULL;KControlStore *c=NULL;
        int bad=kslot_read(bootstrap_save_dir(b),(unsigned)m->selector,(unsigned)m->slot,&f,&c);
        if(bad||!memchr(f->module,0,260)||!f->module[0]){kflags_free(f);kcontrol_free(c);snprintf(m->status,sizeof(m->status),"Slot is empty or unreadable");return;}
        kflags_free(f);kcontrol_free(c);
        if(bootstrap_flush_progress(b)){snprintf(m->status,sizeof(m->status),"Could not save read history");return;}
        m->next=bootstrap_create_split(b->root,bootstrap_save_dir(b));
        if(!m->next||m->next->error[0]||bootstrap_enable_async_voice(m->next)||bootstrap_enable_async_images(m->next)){
            bootstrap_destroy(m->next);m->next=NULL;snprintf(m->status,sizeof(m->status),"Cannot initialize load runtime");return;
        }
        m->loading=1;m->ticks=0;snprintf(m->status,sizeof(m->status),"Loading... B / Backspace: cancel");
    }
}
/* The old runtime stays alive until the target reaches a usable scene. */
static int save_menu_step(SaveMenu *m,KBootstrap **live){
    if(!m->loading)return 0;
    /* Loading runs on the private runtime; use a larger VM slice so parsing
       and resource indexing finish in fewer UI frames without blocking input. */
    bootstrap_frame(m->next);int state=bootstrap_run(m->next,500000);
    if(state<0||state==0||++m->ticks>36000){snprintf(m->status,sizeof(m->status),"Load failed; current game retained");bootstrap_destroy(m->next);m->next=NULL;m->loading=0;return 0;}
    int ready=m->loading==3&&m->next->title.active;
    if((m->loading==1||m->loading==4)&&m->next->title.active){
        if(m->loading==4?bootstrap_load_animation_switch(m->next,(unsigned)m->selector):bootstrap_load_slot(m->next,(unsigned)m->selector,(unsigned)m->slot)){
            bootstrap_destroy(m->next);m->next=NULL;m->loading=0;snprintf(m->status,sizeof(m->status),"Unsupported or damaged save");return 0;
        }m->loading=2;snprintf(m->status,sizeof(m->status),"Restoring scene...");
    }else if(m->loading==2&&!m->next->restore_pending&&(m->next->message_active||m->next->choice_active))ready=1;
    if(ready){bootstrap_destroy(*live);*live=m->next;m->next=NULL;m->loading=m->active=0;m->page_key=0;return 1;}return 0;
}
static void save_menu_clear(SaveMenu *m){
    rmt_free(&m->artwork);rmt_free(&m->canvas);for(unsigned i=0;i<10;i++)rmt_free(&m->previews[i]);SDL_DestroyTexture(m->texture);m->texture=NULL;
}
static void save_menu_draw(SaveMenu *m,KBootstrap *b,SDL_Renderer *r){
    if(ui_canvas(&m->canvas)||ui_asset(b,"saveload_p.rmt",&m->artwork))return;
    int state[]={m->save,m->slot,m->selector,m->overwrite,m->loading,m->focus,m->button};
    uint64_t key=ui_hash(ui_hash(1469598103934665603ULL,state,sizeof(state)),m->status,strlen(m->status));key=ui_hash(key,m->note,strlen(m->note));
    if(key==m->draw_key&&m->texture){SDL_Rect dst={160,0,960,720};SDL_RenderCopy(r,m->texture,NULL,&dst);return;}m->draw_key=key;
    KImage *d=&m->canvas,*a=&m->artwork;ui_fill(d,0,0,640,480,0x16100b);
    ui_blit(d,a,0,0,640,28,0,0,0);ui_blit(d,a,0,m->save?30:60,68,28,4,0,0);
    unsigned page=(unsigned)(m->slot-1)/10,row=(unsigned)(m->slot-1)%10;
    ui_blit(d,a,164+(int)page*44,80,40,22,164+(int)page*44,3,0);
    ui_blit(d,a,0,294,344,272,8,30,0);
    if(m->previews[row].pixels)ui_blit(d,&m->previews[row],0,0,336,252,12,40,0);
    else ui_text(b,d,m->valid[row]?"画像なし":"データがありません",56,142,268,24,0xeee6cc,0);
    const char *groups[]={"本編","附録 1","附録 2","附録 3"};ui_text(b,d,groups[m->selector],176,318,160,20,0xf4d574,0);
    ui_text(b,d,"X: 切替",176,344,160,20,0xffffff,0);
    for(unsigned i=0;i<6;i++){
        int sx=i<3?166+(int)i*150:166+(int)(i-3)*150,sy=i<3?104:200;
        int disabled=i==0&&!bootstrap_can_save(b);sy+=disabled?72:m->focus&&m->button==(int)i?48:0;
        ui_blit(d,a,sx,sy,150,24,10,316+(int)i*26,0);
    }
    for(unsigned i=0;i<10;i++){
        ui_blit(d,a,348,i==row&&!m->focus?346:302,274,44,364,35+(int)i*44,0);
        char line[80];snprintf(line,sizeof(line),"%03u  %s",page*10+i+1,m->valid[i]?"":"---");
        ui_text(b,d,line,376,39+(int)i*44,244,20,0xffdf6c,0);
        if(m->comments[i][0])ui_text(b,d,m->comments[i],412,39+(int)i*44,220,20,0xeee6cc,1);
        if(m->valid[i]){
            time_t stamp=(time_t)m->dates[i];struct tm *tm=stamp?localtime(&stamp):NULL;
            if(tm)strftime(line,sizeof(line),"%Y/%m/%d %H:%M",tm);else snprintf(line,sizeof(line),"%.50s",m->labels[i]);
            ui_text(b,d,line,376,59+(int)i*44,248,18,0xeee6cc,0);
        }
    }
    ui_text(b,d,"L/R: ページ",176,390,180,20,0xffffff,0);
    ui_text(b,d,"Y: SAVE/LOAD",176,416,180,20,0xffffff,0);
    ui_text(b,d,"A: 決定 B: 戻る",176,444,180,20,0xffffff,0);
    if(m->status[0]&&!m->overwrite&&!m->loading){ui_fill(d,16,264,324,32,0x241a12);ui_text(b,d,m->status,20,270,316,24,0xffdf78,0);}
    if(m->overwrite||m->loading){
        ui_fill(d,170,200,300,158,0xa08948);ui_fill(d,173,203,294,152,0x241a12);
        ui_text(b,d,m->overwrite?"コメント（全角 16 文字まで）":m->loading?"ロード中…":m->status,188,224,260,64,0xffffff,0);
        if(m->overwrite){ui_fill(d,184,255,272,32,0x0c0906);ui_text(b,d,m->note,188,262,264,20,0xffffff,0);ui_text(b,d,m->status,188,289,264,20,0xffd57a,0);}
        ui_text(b,d,m->overwrite?"A: 保存 B: 戻る\nY: 入力 X: 消去":"B: 戻る",198,312,260,32,0xffde7a,0);
    }
    ui_present(r,d,&m->texture);
}

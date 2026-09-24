#include <time.h>
#include "dialog.h"
/* Kisaku CSaveDialog / CLoadDialog; port files remain isolated from originals. */
typedef struct {
    int active,save,slot,selector,overwrite,loading,confirm_load;unsigned ticks;
    char status[100],labels[10][64],comments[10][43],note[129];KBootstrap *next;
    int page_key,focus,button,request;unsigned valid[10];int64_t dates[10];
    uint64_t draw_key;KImage artwork,load_artwork,common,canvas,previews[10];SDL_Texture *texture;
    KFlags *states[10];KSlotInfo info[10];int last_slot,hover,areas[31][5];unsigned area_count;
    unsigned live_character,portrait_frame,portrait_manual;int portrait_stick;uint64_t portrait_started,portrait_repeat;KImage portrait;
    KImage detail_image;int detail_hover;unsigned detail_view,detail_phase,detail_step,detail_steps;uint64_t detail_started;
    KImage dialog_artwork,note_artwork,detail_artwork,detail_status_artwork;unsigned param_detail,param_steps,character_detail,detail_index;uint64_t param_started;
} SaveMenu;
#include "character_detail.inc"
static void save_menu_refresh(SaveMenu *m,const char *root){
    unsigned first=(unsigned)(m->slot-1)/10*10+1;
    int key=m->selector*10+(m->slot-1)/10+1;if(m->page_key==key)return;m->page_key=key;m->draw_key=0;
    for(unsigned i=0;i<10;i++){
        rmt_free(&m->previews[i]);kflags_free(m->states[i]);m->states[i]=NULL;memset(&m->info[i],0,sizeof(m->info[i]));m->dates[i]=0;m->valid[i]=0;m->comments[i][0]=0;
        KFlags *f=NULL;KControlStore *c=NULL;
        int bad=kslot_read(root,(unsigned)m->selector,first+i,&f,&c);
        if(!bad&&c->count&&memchr(f->module,0,260)&&f->module[0]&&f->byte_count==9192&&f->word_count==600&&f->raw_count==15000&&!kslot_info(f,first+i,&m->info[i])){
            m->valid[i]=1;snprintf(m->labels[i],64,"%03u  %.40s",first+i,(char *)f->module);
            memcpy(m->comments[i],m->info[i].comment,sizeof(m->comments[i]));m->dates[i]=m->info[i].saved_time;
            m->states[i]=f;f=NULL;
        }else snprintf(m->labels[i],64,"%03u",first+i);
        kflags_free(f);kcontrol_free(c);
    }
}
static void save_menu_open(SaveMenu *m,KBootstrap *b,int load){
    if(m->active||b->scene_replay)return;
    m->active=1;m->save=!load;m->slot=1;
    if(b->raw_size>=1002){unsigned last=b->raw_variables[1000]|(unsigned)b->raw_variables[1001]<<8;if(last<100)m->slot=(int)last+1;}
    m->last_slot=m->slot;m->hover=0x100+(m->slot-1)%10;
    m->selector=b->vm->globals[1][61].number==1;m->overwrite=m->confirm_load=0;m->param_detail=m->character_detail=0;m->detail_index=0;rmt_free(&m->detail_artwork);rmt_free(&m->detail_status_artwork);m->button=2;m->status[0]=0;m->focus=0;m->page_key=0;
    save_menu_refresh(m,bootstrap_save_dir(b));
}
/* action: 0 confirm, 1 back, 2 up, 3 down, 4 previous page, 5 next page,
   6 toggle save/load, 7 open the selected slot's parameter preview. */
static void save_menu_action(SaveMenu *m,KBootstrap *b,int action){
    int load_confirmed=0;
    if(m->character_detail){character_detail_action(m,b,action);return;}
    if(m->param_detail){if(SDL_GetTicks64()-m->param_started>=(uint64_t)m->param_steps*15&&(action==0||action==1))m->param_detail=0;return;}
    if(m->loading){if(action==1){bootstrap_destroy(m->next);m->next=NULL;m->loading=0;snprintf(m->status,sizeof(m->status),"Load cancelled");}return;}
    if(action==1&&(m->overwrite||m->confirm_load)){SDL_StopTextInput();m->overwrite=m->confirm_load=0;m->status[0]=0;return;}
    if(action==1){m->active=0;m->overwrite=0;b->load_modal=b->file_modal=0;b->exec_status&=m->save?~16u:~8u;bootstrap_message_hide(b,0);return;}
    if(m->overwrite||m->confirm_load){
        if(action==2||action==4)m->button=1;
        if(action==3||action==5)m->button=0;
        if(!action){
            if(m->button>1)return;
            if(!m->button){save_menu_action(m,b,1);return;}
            if(m->confirm_load){load_confirmed=1;m->confirm_load=0;}
        }
        if(m->confirm_load)return;
    }
    if(m->overwrite&&action){
        if(action==7)m->note[0]=0;
#ifdef __SWITCH__
        if(action==6){SwkbdConfig keyboard;char note[129]={0};if(R_SUCCEEDED(swkbdCreate(&keyboard,0))){
            swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetInitialText(&keyboard,m->note);swkbdConfigSetStringLenMax(&keyboard,27);swkbdConfigSetStringLenMin(&keyboard,0);
            if(R_SUCCEEDED(swkbdShow(&keyboard,note,sizeof(note))))snprintf(m->note,sizeof(m->note),"%s",note);
            swkbdClose(&keyboard);
        }}
#endif
        return;
    }
    if(action==4||action==5){save_menu_action(m,b,action==4?8:9);return;}
    if(m->focus==2&&!action){save_menu_action(m,b,1);return;}
    if(m->focus==2&&(action==2||action==3)){m->focus=0;return;}
    if(action==2||action==3||action==8||action==9){int delta=action==2?-1:action==3?1:action==8?-10:10;m->focus=0;m->slot=(m->slot-1+delta+100)%100+1;m->overwrite=0;m->status[0]=0;save_menu_refresh(m,bootstrap_save_dir(b));return;}
    if(action==6){m->focus=0;m->save=!m->save;m->selector=b->vm->globals[1][61].number==1;m->overwrite=0;m->status[0]=0;save_menu_refresh(m,bootstrap_save_dir(b));return;}
    if(action==7){
        unsigned row=(unsigned)(m->slot-1)%10;
        if(m->selector||m->focus||!m->states[row])return;
        if(!m->previews[row].pixels&&bootstrap_saved_param_image(b,m->states[row],&m->previews[row])){snprintf(m->status,sizeof(m->status),"パラメータを読み込めませんでした");return;}
        int speed=bootstrap_message_setting(b,8,0);
        m->param_steps=b->vm->globals[0][50].number&0x4000?24:speed==2?0:speed==1?6:12;
        m->param_started=SDL_GetTicks64();m->param_detail=1;return;
    }
    if(action)return;
    if(m->save){
        if(!bootstrap_can_save(b)){snprintf(m->status,sizeof(m->status),"Save requires a settled dialogue checkpoint");return;}
        if(!m->overwrite){m->overwrite=1;m->button=2;ui_utf8_note(m->comments[(m->slot-1)%10],m->note);m->status[0]=0;SDL_StartTextInput();return;}
        uint8_t encoded[33];size_t length;
        if(m->note[0]&&(ktext_name_encode(m->note,encoded,&length)||length>27)){snprintf(m->status,sizeof(m->status),"全角13文字まで入力できます");return;}
        int failed=bootstrap_save_slot_comment(b,(unsigned)m->slot,m->note);m->overwrite=0;SDL_StopTextInput();
        /* 失败时把存档根一并显示：NSP 下应是 save:，回退时是 SD 路径，
           落在 romfs 就说明存档根接错了。 */
        if(failed){
            snprintf(m->status,sizeof(m->status),"保存に失敗しました（旧データを保持）[%.24s]",bootstrap_save_dir(b));
        }else {snprintf(m->status,sizeof(m->status),"保存しました");m->last_slot=m->slot;}
        m->page_key=0;save_menu_refresh(m,bootstrap_save_dir(b));
    }else{
        KFlags *f=NULL;KControlStore *c=NULL;
        int bad=kslot_read(bootstrap_save_dir(b),(unsigned)m->selector,(unsigned)m->slot,&f,&c);
        if(bad||!memchr(f->module,0,260)||!f->module[0]){kflags_free(f);kcontrol_free(c);snprintf(m->status,sizeof(m->status),"Slot is empty or unreadable");return;}
        kflags_free(f);kcontrol_free(c);
        if(!load_confirmed){m->confirm_load=1;m->button=2;return;}
        if(bootstrap_flush_progress(b)){snprintf(m->status,sizeof(m->status),"Could not save read history");return;}
        m->next=bootstrap_create_split(b->root,bootstrap_save_dir(b));
        if(!m->next||m->next->error[0]||bootstrap_enable_async_voice(m->next)||bootstrap_enable_async_images(m->next)){
            bootstrap_destroy(m->next);m->next=NULL;snprintf(m->status,sizeof(m->status),"Cannot initialize load runtime");return;
        }
        m->loading=1;m->ticks=0;snprintf(m->status,sizeof(m->status),"Loading... B / Backspace: cancel");
    }
}
/* CParamaterWnd uses the same 102/103 skip inputs as CConfig fades.  The
   modal is purely a presentation layer, so finishing it leaves the selected
   slot and its cached surface untouched. */
static void save_menu_param_skip(SaveMenu *m){
    if(m->param_detail)m->param_started=SDL_GetTicks64()-(uint64_t)m->param_steps*15;
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
    rmt_free(&m->portrait);rmt_free(&m->detail_image);
    rmt_free(&m->dialog_artwork);rmt_free(&m->note_artwork);rmt_free(&m->detail_artwork);rmt_free(&m->detail_status_artwork);
    rmt_free(&m->artwork);rmt_free(&m->load_artwork);rmt_free(&m->common);rmt_free(&m->canvas);for(unsigned i=0;i<10;i++){rmt_free(&m->previews[i]);kflags_free(m->states[i]);m->states[i]=NULL;}SDL_DestroyTexture(m->texture);m->texture=NULL;
}
#include "save_draw.inc"

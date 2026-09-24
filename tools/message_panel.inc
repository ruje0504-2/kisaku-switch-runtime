#include "dialog.h"
#include "voice_character.h"
#include "text_layout.h"
#include "config_audio.h"
#include "../runtime/name_tables.h"
typedef struct {int id,x0,y0,x1,y1;} ConfigArea;
typedef struct KNawaPanel KNawaPanel;
typedef struct {unsigned kind,selected,back,viewing,variant,scene_request,scene_cancel,cg_zoom;int cg_x,cg_y;char status[512],name[129];
    /* CName (0047b380) keeps a five-character CP932 buffer and a separate
       grid/category cursor.  These fields mirror that transient state so the
       date/name modal does not fall back to the Switch system keyboard. */
    unsigned name_mode,name_page,name_cursor,name_focus,name_confirm,name_text_cursor;
    KImage image;SDL_Texture *texture;KTextureCache cg_frame;
    unsigned direct_scene,direct_selected,direct_page,direct_count;int direct_focus,direct_sub;KImage direct_artwork,direct_parts,direct_thumb;char direct_thumb_name[64];
    KImage direct_from,direct_background;unsigned direct_frames[9],direct_unlocked[9];
    unsigned direct_motion,direct_steps,direct_alpha,direct_thumb_serial;int direct_result,direct_hover;
    uint64_t direct_clock,direct_hover_clock;
    unsigned cg_scroll_active,cg_scroll_limit,cg_scroll_step,cg_movie_return;uint64_t cg_scroll_start;
    SDL_Texture *cg_textures[3];unsigned cg_cached,cg_group,cg_page,cg_count;
    unsigned replay_key,replay_from,replay_to,replay_alpha;uint64_t replay_next;
    KNawaPanel *nawa;
    KImage name_artwork,name_grid_cache,name_help,dialog_artwork,dialog_body;SDL_Texture *name_help_texture;unsigned name_grid_mode,name_grid_page,name_grid_valid;
    KImage nav_artwork,nav_scene,nav_previews[4];unsigned nav_catalog_ready,nav_catalog[100];unsigned nav_id,nav_mode,nav_count,nav_slots[100],nav_page_key,nav_load_request;int64_t nav_dates[4];
    KImage history_artwork,settings_artwork,sidebar_artwork;
    /* Native appendix atlases.  Music/video use AKB layers directly; keeping
       them on the panel avoids decoding 345 KiB/583 KiB every frame. */
    KImage appendix_artwork,appendix_parts;
    uint64_t native_key;unsigned native_kind;int setting_values[KSET_COUNT],setting_ready;unsigned setting_page,return_title,dismiss_menus,animation_request;
    KImage config_art[5];ConfigArea config_areas[4][68];unsigned config_area_count[4],config_mouse_drag;int config_hover,config_voice_snapshot[33];
    ConfigArea backlog_areas[9];unsigned backlog_area_count,backlog_offset,backlog_mouse_drag;int backlog_hover;
    KBacklogVoices voice_sequence;unsigned voice_next;KBootstrap *voice_runtime;
    KVoiceWorker *voice_worker;uint8_t *voice_pcm;size_t voice_size,voice_at;unsigned voice_loading,voice_serial;
    KConfigAudio config_audio;unsigned config_audio_serial;
    unsigned config_motion,config_steps,config_step,config_target,config_next_kind;
    uint64_t config_clock;int config_dx,config_dy,config_preview_dx;unsigned config_alpha;
} MessagePanel;

static unsigned name_utf8_count(const char *text){
    unsigned count=0;const unsigned char *p=(const unsigned char *)text;
    while(*p){unsigned char c=*p++;if(c<0x80){count++;continue;}
        unsigned more=c<0xe0?1:c<0xf0?2:3;for(unsigned i=0;i<more&&*p;i++)p++;count++;}
    return count;
}
static unsigned name_offsets(const char *text,size_t offsets[6]){
    unsigned count=0;size_t n=0;offsets[0]=0;
    while(text[n]&&count<5){unsigned char c=(unsigned char)text[n];size_t step=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(!text[n+step-1])break;
        n+=step;offsets[++count]=n;
    }
    return count;
}
static int name_insert_text(MessagePanel *p,const char *text){
    if(!text||!*text)return 0;
    size_t offsets[6];unsigned count=name_offsets(p->name,offsets);
    unsigned add_count=name_utf8_count(text);
    size_t n=strlen(p->name),m=strlen(text);
    if(!add_count||count+add_count>5||n+m>=sizeof(p->name))return -1;
    unsigned cursor=p->name_text_cursor>count?count:p->name_text_cursor;
    size_t pos=offsets[cursor];
    memmove(p->name+pos+m,p->name+pos,n-pos+1);
    memcpy(p->name+pos,text,m);
    p->name_text_cursor=cursor+add_count;
    p->status[0]=0;
    return 0;
}
static void name_delete_at(MessagePanel *p){
    size_t offsets[6];unsigned count=name_offsets(p->name,offsets);
    unsigned cursor=p->name_text_cursor>count?count:p->name_text_cursor;
    if(!cursor)return;
    size_t begin=offsets[cursor-1],end=offsets[cursor];
    memmove(p->name+begin,p->name+end,strlen(p->name)-end+1);p->name_text_cursor=cursor-1;p->status[0]=0;
}
static int name_cp932_utf8(uint16_t raw,char out[8]){
    if(raw==0xffff)return -1;
    uint8_t bytes[2];size_t size=raw>0xff?2:1;bytes[0]=(uint8_t)(raw&0xff);bytes[1]=(uint8_t)(raw>>8);
    KTextChar ch;if(ktext_decode(KTEXT_CP932,bytes,size,&ch,1,&size))return -1;
    uint32_t cp=ch.codepoint;size_t n=0;
    if(cp<0x80)out[n++]=(char)cp;
    else if(cp<0x800){out[n++]=(char)(0xc0|(cp>>6));out[n++]=(char)(0x80|(cp&63));}
    else if(cp<0x10000){out[n++]=(char)(0xe0|(cp>>12));out[n++]=(char)(0x80|((cp>>6)&63));out[n++]=(char)(0x80|(cp&63));}
    else{out[n++]=(char)(0xf0|(cp>>18));out[n++]=(char)(0x80|((cp>>12)&63));out[n++]=(char)(0x80|((cp>>6)&63));out[n++]=(char)(0x80|(cp&63));}
    out[n]=0;return 0;
}
static uint16_t name_table_value(const MessagePanel *p,unsigned row,unsigned col){
    if(row>=12||col>=18)return 0xffff;
    if(p->name_mode==0)return k_name_kana[row][col];
    if(p->name_mode==2)return row<6?k_name_symbol[row][col]:0xffff;
    /* The original page jump table reaches 0xb8 (184).  The final
       page contains the last two recovered rows; cells below that are
       sentinel blanks rather than reads past the static table. */
    unsigned page=p->name_page<185?p->name_page:184;
    if(page+row>=186)return 0xffff;
    return k_name_kanji[page+row][col];
}
static int name_append_value(MessagePanel *p,uint16_t raw){
    char add[8];if(name_cp932_utf8(raw,add))return -1;
    size_t offsets[6];unsigned count=name_offsets(p->name,offsets);
    if(count>=5)return -1;
    unsigned cursor=p->name_text_cursor>count?count:p->name_text_cursor;
    size_t pos=offsets[cursor],m=strlen(add),n=strlen(p->name);if(n+m>=sizeof(p->name))return -1;
    memmove(p->name+pos+m,p->name+pos,n-pos+1);memcpy(p->name+pos,add,m);p->name_text_cursor=cursor+1;p->status[0]=0;return 0;
}
/* The CName grid is static between page/category changes. Rendering all 216
   glyphs through FreeType every frame made the modal visibly stutter on the
   Switch; cache that static atlas and keep the cursor highlight dynamic. */
static int name_grid_prepare(MessagePanel *p,KBootstrap *b){
    if(p->name_grid_valid&&p->name_grid_mode==p->name_mode&&p->name_grid_page==p->name_page)return 0;
    rmt_free(&p->name_grid_cache);
    p->name_grid_cache=(KImage){0,0,432,288,1728,calloc(432u*288u,4)};
    if(!p->name_grid_cache.pixels)return -1;
    unsigned rows=p->name_mode==2?6:12;
    for(unsigned row=0;row<rows;row++)for(unsigned col=0;col<18;col++){
        int x=(int)col*24,y=(int)row*24;ui_fill(&p->name_grid_cache,x,y,24,24,0x181c24);
        uint16_t raw=name_table_value(p,row,col);if(raw!=0xffff){char label[8];if(!name_cp932_utf8(raw,label)&&ui_text(b,&p->name_grid_cache,label,x+3,y+3,18,18,0xffffff,0))return -1;}
    }
    p->name_grid_mode=p->name_mode;p->name_grid_page=p->name_page;p->name_grid_valid=1;return 0;
}
static int name_help_prepare(MessagePanel *p,KBootstrap *b){
    if(p->name_help.pixels)return 0;
    p->name_help=(KImage){0,0,144,104,144*4,calloc(144u*104u,4)};
    if(!p->name_help.pixels)return -1;
    ui_fill(&p->name_help,0,0,144,104,0x0c0f14);
    if(ui_text_key(b,&p->name_help,"name.help",0,0,144,104,0xffe9a0)){
        rmt_free(&p->name_help);return -1;
    }
    return 0;
}
static void name_help_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    /* User-requested controls live inside the 160-pixel left margin and use
       the same HOS/system font as the rest of CName.  Cache this static text. */
    if(name_help_prepare(p,b))return;
    if(!p->name_help_texture)p->name_help_texture=kimage_texture(r,&p->name_help);
    if(p->name_help_texture){SDL_Rect dst={8,420,144,104};SDL_RenderCopy(r,p->name_help_texture,NULL,&dst);}
}
#include "history_voice.inc"
#include "gallery_panel.inc"
#include "nawa_panel.inc"
#include "extra_panels.inc"
#include "replay_panel.inc"
#include "settings_panel.inc"
#include "backlog_panel.inc"
/* Monotonic, cooperative scrolling: no delays or repeated image decoding. */
static void gallery_scroll_tick(MessagePanel *p,uint64_t now){
    if(!p->cg_scroll_active)return;
    uint64_t elapsed=now>=p->cg_scroll_start?now-p->cg_scroll_start:0;
    uint64_t steps=elapsed/15;
    if(!p->cg_scroll_step||steps>=(p->cg_scroll_limit+p->cg_scroll_step-1)/p->cg_scroll_step){p->cg_y=(int)p->cg_scroll_limit;p->cg_scroll_active=0;}
    else p->cg_y=(int)(steps*p->cg_scroll_step);
}
static unsigned scene_replay_slots(MessagePanel *p,KBootstrap *b,unsigned slots[600]){
    if(!b->scene||p->selected>=b->scene->count||b->scene_replay)return 0;
    if(!b->scene_history.loaded&&khistory_load(&b->scene_history,bootstrap_save_dir(b)))return 0;
    unsigned count=0,id=b->scene->nodes[p->selected].id;
    for(unsigned i=0;i<600;i++){KSceneCheckpoint cp;if(!khistory_checkpoint(&b->scene_history,i,&cp)&&cp.scene==id&&kscene_module(cp.module)==id)slots[count++]=i;}
    return count;
}
#include "navigation_panel.inc"
static void message_panel_action(MessagePanel *p,KBootstrap *b,int action){
    if(p->kind==22){if(bootstrap_native_cg_action(b,action))snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.cg_failed"));if(!b->native_cg)p->kind=0;return;}
    if(p->kind==6||p->kind==18||p->kind==19||p->kind==21){
        if(action==1){
            if(p->kind==21&&bootstrap_title_reset_close(b,0))return;
            if(p->kind==6)bootstrap_quit_dialog_close(b,0);
            if(p->kind==19&&bootstrap_letter_exit(b,0)){snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.letter_return_failed"));return;}
            p->kind=0;p->viewing=0;b->message_hover=-1;return;
        }
        if(!p->viewing){p->selected=2;p->viewing=1;}
        if(action==2||action==4)p->selected=1;
        if(action==3||action==5)p->selected=0;
        if(!action&&p->selected<2){
            if(p->kind==21){
                if(bootstrap_title_reset_close(b,p->selected==1)){snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.reset_failed"));return;}
            }else if(p->kind==19){
                if(bootstrap_letter_exit(b,p->selected==1)){snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.replay_return_failed"));return;}
            }else if(p->selected==1){
                if(p->kind==18)p->return_title=1;
                else if(bootstrap_quit_dialog_close(b,1)){snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.progress_save_failed"));return;}
            }
            if(p->kind==6&&p->selected==0)bootstrap_quit_dialog_close(b,0);
            p->kind=0;p->viewing=0;b->message_hover=-1;
        }
        return;
    }
    if(p->kind==17){if(action==1)p->scene_cancel=1;return;}
    if(p->kind==20){scene_mode_action(p,b,action);return;}
    if(p->kind==16){navigation_panel_action(p,b,action);return;}
    if(p->kind==4&&(action==7||action==0)){p->kind=16;p->nav_id=0;p->variant=0;p->status[0]=0;navigation_panel_refresh(p,b);return;}
    if(p->kind==10&&p->viewing==2){
        if(action==7){
            if(p->cg_scroll_active){p->cg_y=(int)p->cg_scroll_limit;p->cg_scroll_active=0;return;}
            unsigned extent=bootstrap_gallery_scroll_extent(b,p->back,p->selected,p->variant);
            if(!extent||bootstrap_gallery_scroll_image(b,p->back,p->selected,p->variant,&p->image))return;
            SDL_DestroyTexture(p->texture);p->texture=NULL;p->cg_zoom=1;p->cg_x=p->cg_y=0;
            int speed=0;for(unsigned i=0;i<b->setting_count;i++)if(!strcmp(b->settings[i].section,"Display")&&!strcmp(b->settings[i].key,"EffectSpeed"))speed=atoi(b->settings[i].value);
            p->cg_scroll_limit=extent;p->cg_scroll_step=speed==0?2:speed==1?4:0;
            p->cg_scroll_start=SDL_GetTicks64();p->cg_scroll_active=1;gallery_scroll_tick(p,p->cg_scroll_start);return;
        }
        p->cg_scroll_active=0;
        if(action==0||action==6){p->cg_zoom^=1;p->cg_x=p->cg_y=0;return;}
        if(p->cg_zoom&&action>=2&&action<=5){
            if(action==2)p->cg_y-=32;
            if(action==3)p->cg_y+=32;
            if(action==4)p->cg_x-=32;
            if(action==5)p->cg_x+=32;
            int maxx=p->image.width>640?(int)p->image.width-640:0,maxy=p->image.height>480?(int)p->image.height-480:0;
            if(p->cg_x<0)p->cg_x=0;
            if(p->cg_x>maxx)p->cg_x=maxx;
            if(p->cg_y<0)p->cg_y=0;
            if(p->cg_y>maxy)p->cg_y=maxy;
            return;
        }
    }
    if(p->kind==10&&p->viewing==3){
        if(action==0||action==1){bootstrap_gallery_movie_stop(b);p->viewing=p->cg_movie_return;}
        return;
    }
    if(p->kind==5){
        backlog_panel_action(p,b,action);return;
    }
    if(p->kind==15){
        if(action==1){bootstrap_history_reset(b,0);p->kind=0;}
        else if(!action){if(!bootstrap_history_reset(b,1))p->kind=0;else snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.reset_recovery"));}
        return;
    }
    if(p->kind==14){
        if(p->name_confirm){
            /* The original confirmation box is a horizontal Yes/No pair.
               Keep vertical input as a compatibility alias, but the native
               D-pad left/right actions (4/5) are the primary way to switch
               between the two choices. */
            if(action==2||action==4)p->selected=0;
            else if(action==3||action==5)p->selected=1;
            if(action==1)return;
            if(!action){
                if(p->selected==0){
                    if(!bootstrap_name_submit(b,p->name)){SDL_StopTextInput();p->kind=0;return;}
                    snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.name_unavailable"));p->name_confirm=0;
                }else{p->name_confirm=0;p->name_focus=0;}
            }
            return;
        }
        if(action==7){name_delete_at(p);return;}
        /* CName has no native B/back action.  Keep action 1 inert so a
           leaked controller or platform event cannot tear down the editor. */
        if(action==1)return;
        /* Y cycles the three native CName tables.  It replaces the old
           Switch OS keyboard call: the original always stays in this modal. */
        if(action==6){p->name_mode=(p->name_mode+1)%3;p->name_page=0;p->name_cursor=0;p->name_focus=1;p->status[0]=0;return;}
        if(action>=2&&action<=5){
            if(p->name_focus==0){
                size_t offsets[6];unsigned count=name_offsets(p->name,offsets);
                if(action==2||action==3){p->name_focus=1;return;}
                if(action==4&&p->name_text_cursor)p->name_text_cursor--;
                else if(action==5&&p->name_text_cursor<count)p->name_text_cursor++;
                return;
            }
            if(p->name_focus==1){
                unsigned row=p->name_cursor/18,col=p->name_cursor%18;
                if(action==2){if(row)p->name_cursor-=18;else p->name_focus=0;}
                else if(action==3){if(row+1<(p->name_mode==2?6:12))p->name_cursor+=18;}
                else if(action==4){if(col)p->name_cursor--;}
                else if(action==5){if(col+1<18)p->name_cursor++;}
            }
            return;
        }
        if(!action){
            if(p->name_focus==1){
                unsigned row=p->name_cursor/18,col=p->name_cursor%18;uint16_t raw=name_table_value(p,row,col);
                if(raw!=0xffff&&!name_append_value(p,raw))return;
                snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.name_limit"));return;
            }
            if(!p->name[0]){snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.name_required"));return;}
            p->name_confirm=1;p->selected=0;
        }
        return;
    }
    if(p->kind==8){settings_panel_action(p,b,action);return;}
    if(action==1){if(p->kind==13)nawa_panel_clear(p);if(p->kind==4){b->scene_modal=0;bootstrap_message_hide(b,0);}if(p->kind==12){bootstrap_replay_style(b,-1);p->kind=0;return;}if(p->kind==10&&p->viewing){p->viewing--;return;}if(p->kind==9||p->kind==10||p->kind==11||p->kind==13||p->kind==23)bootstrap_extra_close(b);p->kind=0;return;}
    if(p->kind==13){
        p->status[0]=0;
        if(action>=2&&action<=5){
            if(p->selected<40){unsigned row=p->selected/20,col=p->selected%20;
                if(action==4||action==5)p->selected=row*20+(col+(action==4?19:1))%20;
                else if((action==2&&row)||(action==3&&!row))p->selected=(1-row)*20+col;
                else p->selected=col<6?41:col<18?42:40;
            }else{unsigned bonus=p->selected-40;
                if(action==4)p->selected=40+(bonus+2)%3;
                if(action==5)p->selected=40+(bonus+1)%3;
                if(action==2||action==3){const unsigned column[3]={19,0,17};p->selected=(action==2?20:0)+column[bonus];}
            }
        }
        if(!action){if(!bootstrap_nawa_select(b,p->selected)){nawa_panel_clear(p);p->kind=0;}else snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.scene_locked"));}
        return;
    }
    if(p->kind==23){
        unsigned count=bootstrap_video_count();
        if(action==2)p->selected=(p->selected+count-1)%count;
        if(action==3)p->selected=(p->selected+1)%count;
        if(action==4)p->selected=(p->selected+count-10)%count;
        if(action==5)p->selected=(p->selected+10)%count;
        if(!action){ if(bootstrap_video_select(b,(int)p->selected))snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.locked_or_unavailable")); else p->kind=0; }
        return;
    }
    if(p->kind==11){
        p->status[0]=0;
        if(action==6||action==7){p->back=(p->back+(action==6?1:4))%5;p->selected=0;}
        if(action>=2&&action<=5)p->selected=replay_grid_move(p->back,p->selected,action);
        if(!action){if(!bootstrap_replay_select(b,p->back,p->selected))p->kind=0;else snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.scene_locked"));}
        return;
    }
    if(p->kind==12){
        if(action>=2&&action<=5)p->selected^=1;
        if(!action&&!bootstrap_replay_style(b,(int)p->selected))p->kind=0;
        return;
    }
    if(p->kind==10){
        p->status[0]=0;
        unsigned count=p->viewing?gallery_variant_count(p,b):kgallery_group_count(&b->gallery,p->back)+(p->back==5?14:0);
        if(!count)return;
        unsigned *index=p->viewing?&p->variant:&p->selected;
        if(action>=2&&action<=5){
            if(p->viewing<2)*index=gallery_grid_move(p,b,action);
            else *index=(*index+count+(action==2||action==4?-1:1))%count;
        }
        if(action==8||action==9){
            if(p->viewing>=2)return;
            int direction=action==8?-1:1;
            *index=p->viewing?gallery_variant_page_move(p,b,direction):kgallery_page_move(&b->gallery,p->back,*index,direction);
        }
        if((action==6||action==7)&&!p->viewing){p->back=(p->back+(action==6?1:6))%7;p->selected=0;}
        if(!action&&!p->viewing){
            unsigned images=kgallery_group_count(&b->gallery,p->back);
            if(p->back==5&&p->selected>=images){
                if(!bootstrap_gallery_movie(b,p->selected-images)){p->cg_movie_return=0;p->viewing=3;}
                else snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.movie_unavailable"));
            }else{p->viewing=1;p->variant=0;}
            return;
        }
        if(!action&&p->viewing==1&&p->back==5&&p->selected==45&&p->variant>=4){
            if(!bootstrap_gallery_animation(b,p->variant-4)){p->cg_movie_return=1;p->viewing=3;}
            else snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.animation_unavailable"));
            return;
        }
        if(!action||p->viewing==2){
            if(!bootstrap_gallery_variant(b,p->back,p->selected,p->variant,&p->image)){p->viewing=2;p->cg_zoom=0;p->cg_x=p->cg_y=0;}
            else{p->viewing=1;snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.variant_unavailable"));}
            SDL_DestroyTexture(p->texture);p->texture=NULL;
        }
        return;
    }
    if(p->kind==9){
        unsigned count=bootstrap_music_count(),visible[16],n=0;for(unsigned i=0;i<count&&n<16;i++)if(bootstrap_music_unlocked(b,i))visible[n++]=i;
        p->status[0]=0;
        if(n){unsigned at=0;while(at<n&&visible[at]!=p->selected)at++;if(at>=n)at=0;
            /* CMusicMode registers one area per unlocked row.  Up/down walk
               those rows; left/right are the same row transition on the PC
               control pad and must never land on a locked source cell. */
            if(action==2)at=(at+n-1)%n;
            if(action==3)at=(at+1)%n;
            if(action==4)at=(at+n-1)%n;
            if(action==5)at=(at+1)%n;
            p->selected=visible[at];
        }
        if(action==6)bootstrap_music_stop(b);
        if(!action&&bootstrap_music_select(b,p->selected))snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.track_unavailable"));
        return;
    }
    if(p->kind==8){settings_panel_action(p,b,action);return;
    }else if(p->kind==4){
        if(!b->scene||!b->scene->count)return;
        if(action==6)p->viewing^=1;
        if(action>=2&&action<=5){int dx=action==4?-1:action==5?1:0,dy=action==2?-1:action==3?1:0;
            int next=kscene_move(b->scene,p->selected,dx,dy);if(next>=0)p->selected=(unsigned)next;}
    }else if(p->kind==5){
        if((action==2||action==4)&&p->back+1<b->history_count)p->back++;
        if((action==3||action==5)&&p->back)p->back--;
    }
}
static void message_dialog_pointer(MessagePanel *p,KBootstrap *b,int x,int y,int click){
    if(p->kind!=6&&p->kind!=18&&p->kind!=19)return;
    int hit=kdialog_hit(x,y);p->selected=hit<0?2:(unsigned)hit;p->viewing=1;
    if(click==2)message_panel_action(p,b,1);
    else if(click==1&&hit>=0)message_panel_action(p,b,0);
}
static void message_panel_draw_body(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    if(p->kind==22){
        const KImage *image=bootstrap_native_cg_image(b);
        if(image){
            if(texture_cache_update(&p->cg_frame,r,image,SDL_BLENDMODE_BLEND))snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.cg_upload_failed"));
            else{SDL_Rect dst={160,0,960,720};SDL_RenderCopy(r,p->cg_frame.texture,NULL,&dst);}
        }
        if(p->status[0])panel_text(b,r,208,690,p->status);
        return;
    }
    if(p->kind!=5&&p->kind!=8){p->native_kind=0;p->native_key=0;}
    if(p->kind==16){navigation_panel_draw(p,b,r);return;}
    if(p->kind==8){settings_panel_draw(p,b,r);return;}
    if(p->kind==5){backlog_panel_draw(p,b,r);return;}
    if(p->kind==23){
        if(!appendix_assets(p,b,1)&&!ui_canvas(&p->image)){
            ui_scale(&p->image,&p->appendix_artwork,0,0,640,480);
            /* 45c130/45c2f0 copy 15x115 cells from p2 into the two native
               rows. Locked cells remain the base background; unlocked cells
               retain the original numbered cassette artwork. */
            for(unsigned i=0;i<bootstrap_video_count();i++)if(bootstrap_video_unlocked(b,i)){
                int col=(int)(i%39),row=(int)(i/39);
                appendix_blit(&p->image,&p->appendix_parts,col*15,row*115,15,115,34+col*15,8+row*115);
            }
            appendix_rect(&p->image,34+(int)(p->selected%39)*15,8+(int)(p->selected/39)*115,15,115,0xf0d060);
            if(p->status[0])ui_text(b,&p->image,p->status,16,452,608,20,0xffd070,0);
            ui_present(r,&p->image,&p->texture);return;
        }
        SDL_SetRenderDrawColor(r,16,20,28,255);SDL_Rect box={208,64,864,592};SDL_RenderFillRect(r,&box);
        panel_text(b,r,272,108,ui_string(b,"menu.video.title"));unsigned first=p->selected/10*10;
        for(unsigned i=first;i<bootstrap_video_count()&&i<first+10;i++){char line[120];snprintf(line,sizeof(line),ui_string(b,"menu.video.item"),i==p->selected?">":" ",i+1,bootstrap_video_preview_name(i),ui_string(b,bootstrap_video_unlocked(b,i)?"common.available":"common.locked"));panel_text(b,r,280,160+(int)(i%10)*36,line);}
        if(p->status[0])panel_text(b,r,272,560,p->status);
        return;
    }
    if(p->kind==11&&!replay_panel_draw(p,b,r,SDL_GetTicks64()))return;
    if(p->kind==9&&!music_panel_draw(p,b,r))return;
    if(p->kind==12&&!style_panel_draw(p,b,r))return;
    if(p->kind==13&&!nawa_panel_draw(p,b,r,SDL_GetTicks64()))return;
    if(p->kind==10&&!p->viewing&&!gallery_panel_draw(p,b,r))return;
    if(p->kind==10&&p->viewing==1&&!gallery_variants_draw(p,b,r))return;
    if(p->kind==10&&p->viewing==3){
        if(b->video_active){SDL_SetRenderDrawColor(r,240,240,240,255);return;}
        bootstrap_gallery_movie_stop(b);p->viewing=p->cg_movie_return;
    }
    SDL_SetRenderDrawColor(r,16,20,28,255);SDL_Rect box={208,64,864,592};SDL_RenderFillRect(r,&box);
    SDL_SetRenderDrawColor(r,240,240,240,255);
    if(p->kind==20){scene_mode_draw(p,b,r);return;}
    if(p->kind==16||p->kind==17){
        panel_text(b,r,272,108,ui_string(b,"menu.navigation.title"));
        if(p->kind==17){panel_text(b,r,272,240,ui_string(b,"status.replay_preparing"));return;}
        unsigned slots[600],count=scene_replay_slots(p,b,slots);
        if(!count)panel_text(b,r,272,240,ui_string(b,"status.replay_no_checkpoint"));
        for(unsigned i=0;i<count;i++){
            KSceneCheckpoint cp;char line[100];khistory_checkpoint(&b->scene_history,slots[i],&cp);
            snprintf(line,sizeof(line),ui_string(b,"menu.navigation.item"),p->variant==i?">":" ",cp.part);panel_text(b,r,280,180+(int)i*32,line);
        }
        if(p->status[0])panel_text(b,r,272,544,p->status);
        return;
    }
    if(p->kind==14){
        if(ui_canvas(&p->image)||ui_akb_asset(b,"namepart.akb",&p->name_artwork))return;
        ui_scale(&p->image,&b->layers[0],0,0,640,480);
        /* CName's atlas has three category buttons followed by four command
           buttons.  The PC positions are x=72,144,216 then 288..504. */
        for(unsigned i=0;i<3;i++)ui_blit(&p->image,&p->name_artwork,(int)i*64,p->name_mode==i?0:24,64,24,72+(int)i*72,368,1);
        for(unsigned i=0;i<4;i++)ui_blit(&p->image,&p->name_artwork,(int)(3+i)*64,24,64,24,288+(int)i*72,368,1);
        /* The native grid is 18 columns by 12 rows, 24 pixels per cell, at
           (128,32).  Draw its CP932 labels from the recovered CName tables;
           the atlas supplies the same gold frame and the selected cell is
           kept separate so movement never changes the story VM. */
        if(name_grid_prepare(p,b))return;
        for(unsigned row=0;row<288;row++)memcpy(p->image.pixels+(32+row)*p->image.stride+128*4,
            p->name_grid_cache.pixels+row*p->name_grid_cache.stride,432*4);
        if(p->name_focus==1){unsigned row=p->name_cursor/18,col=p->name_cursor%18;
            if(row<(p->name_mode==2?6:12)){
                int x=128+(int)col*24,y=32+(int)row*24;
                ui_fill(&p->image,x,y,24,24,0x806020);
                uint16_t raw=name_table_value(p,row,col);char label[8];
                if(raw!=0xffff&&!name_cp932_utf8(raw,label))ui_text(b,&p->image,label,x+3,y+3,18,18,0xffffff,0);
            }
        }
        /* CName's kanji page bar is a separate set of hit areas at y=328:
           ten kana-row jumps and one-step previous/next buttons.  The
           namepart atlas stores normal buttons at y=64 and pressed buttons
           at y=48, matching the category controls above. */
        if(p->name_mode==1){
            static const unsigned targets[]={0,14,58,104,129,136,160,167,173,184};
            unsigned active=0;
            for(unsigned i=0;i<10;i++)if(p->name_page==targets[i]){active=i;break;}
            for(unsigned i=0;i<10;i++)ui_blit(&p->image,&p->name_artwork,(int)i*32,active==i?48:64,32,16,80+(int)i*36,328,1);
            ui_blit(&p->image,&p->name_artwork,320,p->name_page?48:64,48,16,460,328,1);
            ui_blit(&p->image,&p->name_artwork,368,p->name_page<184?48:64,48,16,512,328,1);
        }
        /* Native name slots are x=260+24*n, y=414.  Draw one decoded
           codepoint per slot instead of laying the UTF-8 string as a normal
           paragraph; the latter lets a wide glyph or an invalid byte shift
           every following slot to the right. */
        ui_fill(&p->image,256,406,128,30,0x080b12);ui_fill(&p->image,256,406,128,2,0xd0a83c);ui_fill(&p->image,256,434,128,2,0xd0a83c);
        for(unsigned i=1;i<5;i++)ui_fill(&p->image,260+(int)i*24,408,1,24,0x5c4c20);
        size_t name_bytes[6];unsigned name_count=name_offsets(p->name,name_bytes);
        for(unsigned i=0;i<name_count;i++){
            char glyph[5]={0};size_t size=name_bytes[i+1]-name_bytes[i];if(size>4)continue;
            memcpy(glyph,p->name+name_bytes[i],size);
            /* name is UTF-8 from SDL input/the CP932 table converter, unlike
               raw MES text. ui_text's encoded=0 path decodes it as UTF-8. */
            int advance=(unsigned char)glyph[0]<0x80?8:16,x=260+(int)i*24+(24-advance)/2;
            ui_text(b,&p->image,glyph,x,412,advance,20,0xffffff,0);
        }
        if(p->name_focus==0)ui_fill(&p->image,259+(int)p->name_text_cursor*24,408,2,24,0xf4d45a);
        if(p->status[0])ui_text(b,&p->image,p->status,128,8,384,20,0xff8a7a,0);
        if(p->name_confirm){
            ui_fill(&p->image,80,188,480,116,0x080b12);ui_fill(&p->image,80,188,480,2,0xd0a83c);ui_fill(&p->image,80,302,480,2,0xd0a83c);
            ui_text_key(b,&p->image,"dialog.name_confirm",176,208,288,24,0xffffff);
            char yes[32],no[32];snprintf(yes,sizeof(yes),"%s%s",p->selected==0?"> ":"  ",ui_string(b,"common.yes"));snprintf(no,sizeof(no),"%s%s",p->selected==1?"> ":"  ",ui_string(b,"common.no"));
            ui_text(b,&p->image,yes,210,258,90,20,p->selected==0?0xffe36a:0xffffff,0);
            ui_text(b,&p->image,no,348,258,110,20,p->selected==1?0xffe36a:0xffffff,0);
        }
        ui_present(r,&p->image,&p->texture);
        name_help_draw(p,b,r);
        return;
    }
    if(p->kind==15){
        if(ui_canvas(&p->image))return;
        ui_scale(&p->image,&b->layers[0],0,0,640,480);ui_fill(&p->image,112,122,416,262,0xa08948);ui_fill(&p->image,115,125,410,256,0x291e14);
        ui_text_key(b,&p->image,"dialog.history_reset",136,144,368,24,0xffffff);
        const char *labels[]={"history.read","history.choice","history.navigation","history.all"};unsigned row=0;
        for(unsigned i=0;i<4;i++)if(b->flag_dialog.checked[i]==1)ui_text_key(b,&p->image,labels[i],144,186+(int)row++*28,352,24,0xffdf78);
        ui_present(r,&p->image,&p->texture);return;
    }
    if(p->kind==6||p->kind==18||p->kind==19||p->kind==21){
        if(!p->viewing){p->selected=2;p->viewing=1;}
        if(ui_canvas(&p->image)||ui_asset(b,"kisaku_DL_dialog_p.akb",&p->dialog_artwork))return;
        if(kdialog_draw(&p->image,&p->dialog_body,&b->layers[0],&p->dialog_artwork,p->kind==21?2:p->kind==19?7:p->kind==18,(int)p->selected))return;
        if(p->status[0])ui_text(b,&p->image,p->status,112,292,416,24,0xffffff,0);
        ui_present(r,&p->image,&p->texture);return;
    }
    if(p->kind==8){
        char title[80];snprintf(title,sizeof(title),ui_string(b,"settings.title"),p->selected/7+1,(bootstrap_setting_count()+6)/7);panel_text(b,r,272,108,title);
        unsigned first=p->selected/7*7;
        for(unsigned i=first;i<first+7&&i<bootstrap_setting_count();i++){
            char line[90],value[24];int current=bootstrap_message_setting(b,i,0),limit=bootstrap_setting_limit(i);
            if(limit==1)snprintf(value,sizeof(value),"%s",ui_string(b,current?"common.on":"common.off"));
            else if(limit==2)snprintf(value,sizeof(value),"%s",ui_string(b,current==0?"common.normal":current==1?"common.fast":"common.instant"));
            else snprintf(value,sizeof(value),ui_string(b,"settings.number"),current,limit);
            snprintf(line,sizeof(line),ui_string(b,"settings.item"),p->selected==i?">":" ",ui_string(b,bootstrap_setting_label(i)),value);panel_text(b,r,280,166+(int)(i-first)*48,line);
        }

        panel_text(b,r,272,566,p->status[0]?p->status:ui_string(b,"settings.footer"));return;
    }
    if(p->kind==13){
        panel_text(b,r,272,108,ui_string(b,"menu.nawa.title"));unsigned first=p->selected/10*10;
        for(unsigned i=first;i<43&&i<first+10;i++){
            char line[100];if(i<40)snprintf(line,sizeof(line),ui_string(b,"menu.nawa.item"),i==p->selected?">":" ",i+1,ui_string(b,bootstrap_nawa_unlocked(b,i)?"common.available":"common.locked"));
            else snprintf(line,sizeof(line),ui_string(b,"menu.nawa.bonus"),i==p->selected?">":" ",i-39);
            panel_text(b,r,280,160+(int)(i%10)*36,line);
        }
        if(p->status[0])panel_text(b,r,272,560,p->status);
        return;
    }
    if(p->kind==11){
        const char *names[]={"Anna","Maki","Suzune","Mika","Natsuko"};char line[100];
        snprintf(line,sizeof(line),ui_string(b,"menu.replay.title"),names[p->back],p->back+1);panel_text(b,r,240,36,line);
        for(unsigned i=0;i<bootstrap_replay_count(p->back);i++){
            const unsigned *xy=replay_positions[p->back<3?0:p->back-2][i];
            int x=160+(int)xy[0]*3/2,y=(int)xy[1]*3/2;
            snprintf(line,sizeof(line),ui_string(b,"menu.replay.item"),i==p->selected?">":" ",i+1);panel_text(b,r,x,y,line);
            panel_text(b,r,x,y+24,ui_string(b,bootstrap_replay_unlocked(b,p->back,i)?"common.available":"common.locked"));
        }
        if(p->status[0])panel_text(b,r,272,554,p->status);
        return;
    }
    if(p->kind==12){
        panel_text(b,r,272,160,ui_string(b,"menu.playback.title"));
        panel_text(b,r,280,260,p->selected==0?ui_string(b,"menu.playback.animation_selected"):ui_string(b,"menu.playback.animation"));
        panel_text(b,r,280,320,p->selected==1?ui_string(b,"menu.playback.still_selected"):ui_string(b,"menu.playback.still"));
        return;
    }
    if(p->kind==10){
        if(p->viewing==2){
            gallery_scroll_tick(p,SDL_GetTicks64());
            if(!p->texture){p->texture=kimage_texture(r,&p->image);if(p->texture)SDL_SetTextureBlendMode(p->texture,SDL_BLENDMODE_NONE);}
            if(p->texture){
                SDL_Rect area={160,0,960,720},dst=kimage_fit(p->image.width,p->image.height,area);
                if(p->cg_zoom){
                    SDL_Rect src={p->cg_x,p->cg_y,p->image.width<640?(int)p->image.width:640,p->image.height<480?(int)p->image.height:480};
                    dst=(SDL_Rect){160+(960-src.w*3/2)/2,(720-src.h*3/2)/2,src.w*3/2,src.h*3/2};SDL_RenderCopy(r,p->texture,&src,&dst);
                }else SDL_RenderCopy(r,p->texture,NULL,&dst);
            }
            SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,12,18,28,210);SDL_Rect footer={160,684,960,36};SDL_RenderFillRect(r,&footer);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r,245,245,245,255);return;
        }else{
            char title[100];snprintf(title,sizeof(title),ui_string(b,"menu.cg.title"),p->back+1,p->viewing?ui_string(b,"menu.cg.variants"):"");panel_text(b,r,272,108,title);
            unsigned selected=p->viewing?p->variant:p->selected;
            unsigned count=p->viewing?gallery_variant_count(p,b):kgallery_group_count(&b->gallery,p->back)+(p->back==5?14:0);
            unsigned first=selected/10*10;
            for(unsigned i=first;i<count&&i<first+10;i++){
                unsigned item=p->viewing?p->selected:i,seen=0,total=kgallery_variant_count(&b->gallery,p->back,item);uint16_t images[3]={0};
                if(p->viewing){kgallery_variant(&b->gallery,p->back,item,i,images);seen=kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,images);}
                else for(unsigned v=0;v<total;v++){kgallery_variant(&b->gallery,p->back,item,v,images);seen+=kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,images);}
                char line[100];
                unsigned image_count=kgallery_group_count(&b->gallery,p->back);
                if(!p->viewing&&p->back==5&&item>=image_count)snprintf(line,sizeof(line),ui_string(b,"menu.cg.movie_item"),i==selected?">":" ",item-image_count+1,ui_string(b,bootstrap_gallery_movie_unlocked(b,item-image_count)?"common.available":"common.locked"));
                else if(p->viewing)snprintf(line,sizeof(line),ui_string(b,"menu.cg.variant_item"),i==selected?">":" ",i+1,ui_string(b,seen?"common.available":"common.locked"));
                else snprintf(line,sizeof(line),ui_string(b,"menu.cg.item"),i==selected?">":" ",i+1,seen,total);
                panel_text(b,r,280,160+(int)(i%10)*36,line);
            }
        }
        if(p->status[0])panel_text(b,r,272,560,p->status);
        return;
    }
    if(p->kind==9){
        panel_text(b,r,272,108,ui_string(b,"menu.music.title"));unsigned first=p->selected/10*10;
        for(unsigned i=first;i<bootstrap_music_count()&&i<first+10;i++){char line[100];snprintf(line,sizeof(line),ui_string(b,"menu.music.item"),i==p->selected?">":" ",i+1,bootstrap_music_name(i));panel_text(b,r,280,160+(int)(i%10)*36,line);}
        return;
    }
    if(p->kind==4){
        if(!navigation_map_draw(p,b)){
            ui_fill(&p->image,0,0,640,24,0x1e1910);ui_fill(&p->image,0,452,640,28,0x1e1910);
            char label[80];snprintf(label,sizeof(label),ui_string(b,"menu.nav_map"),b->scene->nodes[p->selected].id);ui_text(b,&p->image,label,12,3,616,20,0xffdf78,0);
            ui_present(r,&p->image,&p->texture);
        }return;
    }
    if(p->image.pixels&&(p->image.width!=640||p->image.height!=480)){rmt_free(&p->image);SDL_DestroyTexture(p->texture);p->texture=NULL;}
    if(b->history_count&&!bootstrap_history_draw(b,p->back,&p->image)){
        if(!p->texture){
            p->texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,640,480);
#ifndef __SWITCH__
            if(p->texture)SDL_SetTextureScaleMode(p->texture,SDL_ScaleModeNearest);
#endif
        }
        if(p->texture){SDL_UpdateTexture(p->texture,NULL,p->image.pixels,(int)p->image.stride);SDL_Rect dst={224,80,832,544};SDL_RenderCopy(r,p->texture,NULL,&dst);}
    }
    char line[100];snprintf(line,sizeof(line),ui_string(b,"menu.history.title"),b->history_count?p->back+1:0,b->history_count);panel_text(b,r,272,108,line);
    if(p->status[0])panel_text(b,r,272,552,p->status);
    else if(!bootstrap_history_voice(b,p->back))panel_text(b,r,272,552,ui_string(b,"status.history_no_voice"));

}

static void message_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    panel_text_begin();message_panel_draw_body(p,b,r);panel_text_end(r);
}

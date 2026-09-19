typedef struct KNawaPanel KNawaPanel;
typedef struct {unsigned kind,selected,back,viewing,variant,scene_request,scene_cancel,cg_zoom;int cg_x,cg_y;char status[96],name[129];KImage image;SDL_Texture *texture;
    unsigned cg_scroll_active,cg_scroll_limit,cg_scroll_step,cg_movie_return;uint64_t cg_scroll_start;
    SDL_Texture *cg_textures[3];unsigned cg_cached,cg_group,cg_page,cg_count;
    unsigned replay_key,replay_from,replay_to,replay_alpha;uint64_t replay_next;
    KNawaPanel *nawa;
    KImage name_artwork;
    KImage nav_artwork,nav_scene,nav_previews[4];unsigned nav_catalog_ready,nav_catalog[100];unsigned nav_id,nav_mode,nav_count,nav_slots[100],nav_page_key,nav_load_request;int64_t nav_dates[4];
    KImage history_artwork,settings_artwork,sidebar_artwork;uint64_t native_key;unsigned native_kind;int setting_values[24],setting_ready;unsigned setting_page,return_title,dismiss_menus,animation_request;
    KVoiceWorker *voice_worker;uint8_t *voice_pcm;size_t voice_size,voice_at;unsigned voice_loading,voice_serial;
} MessagePanel;
#include "history_voice.inc"
#include "gallery_panel.inc"
#include "nawa_panel.inc"
#include "extra_panels.inc"
#include "replay_panel.inc"
#include "settings_panel.inc"
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
    if(p->kind==18){if(!action){p->return_title=1;p->kind=0;}else if(action==1)p->kind=0;return;}
    if(p->kind==17){if(action==1)p->scene_cancel=1;return;}
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
        if(action==1){if(p->viewing){p->viewing=0;p->cg_y=0;return;}history_voice_stop(p);p->kind=0;p->dismiss_menus=1;return;}
        if(action==6){p->viewing^=1;p->cg_y=0;return;}
        if(p->viewing&&(action==2||action==3)){if(action==2&&p->cg_y)p->cg_y--;if(action==3&&(unsigned)p->cg_y<p->cg_scroll_limit)p->cg_y++;return;}
        unsigned old=p->back;
        if(action==8)p->back=b->history_count?(p->back+6<b->history_count?p->back+6:b->history_count-1):0;
        if(action==9)p->back=p->back>6?p->back-6:0;
        if((action==2||action==4)&&p->back+1<b->history_count)p->back++;
        if((action==3||action==5)&&p->back)p->back--;
        if(old!=p->back){p->cg_y=0;history_voice_stop(p);p->status[0]=0;}
        if(!action)history_voice_play(p,b);
        return;
    }
    if(p->kind==15){
        if(action==1){bootstrap_history_reset(b,0);p->kind=0;}
        else if(!action){if(!bootstrap_history_reset(b,1))p->kind=0;else snprintf(p->status,sizeof(p->status),"Reset failed. Restart to recover; original files are preserved.");}
        return;
    }
    if(p->kind==14){
        if(action==7){p->name[0]=0;p->status[0]=0;return;}
        if(action==1){bootstrap_name_submit(b,NULL);SDL_StopTextInput();p->kind=0;return;}
        if(!action){if(!bootstrap_name_submit(b,p->name)){SDL_StopTextInput();p->kind=0;}else snprintf(p->status,sizeof(p->status),"Use a nonempty CP932 name, at most 32 bytes");}
#ifdef __SWITCH__
        if(action==6){
            SwkbdConfig keyboard;char name[129]={0};
            if(R_SUCCEEDED(swkbdCreate(&keyboard,0))){
                swkbdConfigMakePresetUserName(&keyboard);swkbdConfigSetInitialText(&keyboard,p->name);swkbdConfigSetStringLenMax(&keyboard,16);swkbdConfigSetStringLenMin(&keyboard,1);
                if(R_SUCCEEDED(swkbdShow(&keyboard,name,sizeof(name)))){snprintf(p->name,sizeof(p->name),"%s",name);p->status[0]=0;}
                swkbdClose(&keyboard);
            }
        }
#endif
        return;
    }
    if(p->kind==8){settings_panel_action(p,b,action);return;}
    if(action==1){if(p->kind==13)nawa_panel_clear(p);if(p->kind==4){b->scene_modal=0;bootstrap_message_hide(b,0);}if(p->kind==12){bootstrap_replay_style(b,-1);p->kind=0;return;}if(p->kind==10&&p->viewing){p->viewing--;return;}if(p->kind==9||p->kind==10||p->kind==11||p->kind==13)bootstrap_extra_close(b);p->kind=0;return;}
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
        if(!action){if(!bootstrap_nawa_select(b,p->selected)){nawa_panel_clear(p);p->kind=0;}else snprintf(p->status,sizeof(p->status),"This scene is not unlocked yet");}
        return;
    }
    if(p->kind==11){
        p->status[0]=0;
        if(action==6||action==7){p->back=(p->back+(action==6?1:4))%5;p->selected=0;}
        if(action>=2&&action<=5)p->selected=replay_grid_move(p->back,p->selected,action);
        if(!action){if(!bootstrap_replay_select(b,p->back,p->selected))p->kind=0;else snprintf(p->status,sizeof(p->status),"This scene is not unlocked yet");}
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
                else snprintf(p->status,sizeof(p->status),"This movie is locked or could not be loaded");
            }else{p->viewing=1;p->variant=0;}
            return;
        }
        if(!action&&p->viewing==1&&p->back==5&&p->selected==45&&p->variant>=4){
            if(!bootstrap_gallery_animation(b,p->variant-4)){p->cg_movie_return=1;p->viewing=3;}
            else snprintf(p->status,sizeof(p->status),"Animation is locked or could not be loaded");
            return;
        }
        if(!action||p->viewing==2){
            if(!bootstrap_gallery_variant(b,p->back,p->selected,p->variant,&p->image)){p->viewing=2;p->cg_zoom=0;p->cg_x=p->cg_y=0;}
            else{p->viewing=1;snprintf(p->status,sizeof(p->status),"This variant is locked or its image could not be loaded");}
            SDL_DestroyTexture(p->texture);p->texture=NULL;
        }
        return;
    }
    if(p->kind==9){
        unsigned count=bootstrap_music_count();p->status[0]=0;
        if(action==2)p->selected=(p->selected+count-1)%count;
        if(action==3)p->selected=(p->selected+1)%count;
        if(action==4)p->selected=(p->selected+count-(10%count))%count;
        if(action==5)p->selected=(p->selected+10)%count;
        if(action==6)bootstrap_music_stop(b);
        if(!action&&bootstrap_music_select(b,p->selected))snprintf(p->status,sizeof(p->status),"This track is locked or unavailable");
        return;
    }
    if(p->kind==6){if(!action){b->quit_requested=1;p->kind=0;}return;}
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
#include "backlog_panel.inc"
static void message_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    if(p->kind!=5&&p->kind!=8){p->native_kind=0;p->native_key=0;}
    if(p->kind==16){navigation_panel_draw(p,b,r);return;}
    if(p->kind==8){settings_panel_draw(p,b,r);return;}
    if(p->kind==5){backlog_panel_draw(p,b,r);return;}
    if(p->kind==11&&!replay_panel_draw(p,b,r,SDL_GetTicks64()))return;
    if(p->kind==9&&!music_panel_draw(p,b,r))return;
    if(p->kind==12&&!style_panel_draw(p,b,r))return;
    if(p->kind==13&&!nawa_panel_draw(p,b,r,SDL_GetTicks64()))return;
    if(p->kind==10&&!p->viewing&&!gallery_panel_draw(p,b,r))return;
    if(p->kind==10&&p->viewing==1&&!gallery_variants_draw(p,b,r))return;
    if(p->kind==10&&p->viewing==3){
        if(b->video_active){SDL_SetRenderDrawColor(r,240,240,240,255);panel_text(r,256,684,"A / B: return to CG gallery");return;}
        bootstrap_gallery_movie_stop(b);p->viewing=p->cg_movie_return;
    }
    SDL_SetRenderDrawColor(r,16,20,28,255);SDL_Rect box={208,64,864,592};SDL_RenderFillRect(r,&box);
    SDL_SetRenderDrawColor(r,240,240,240,255);
    if(p->kind==16||p->kind==17){
        panel_text(r,272,108,"NAVIGATION - REPLAY COMPLETED SCENE");
        if(p->kind==17){panel_text(r,272,240,"Preparing scene... B: cancel");return;}
        unsigned slots[600],count=scene_replay_slots(p,b,slots);
        if(!count)panel_text(r,272,240,"No matching completed checkpoint for this scene.");
        for(unsigned i=0;i<count;i++){
            KSceneCheckpoint cp;char line[100];khistory_checkpoint(&b->scene_history,slots[i],&cp);
            snprintf(line,sizeof(line),"%s Part %u",p->variant==i?">":" ",cp.part);panel_text(r,280,180+(int)i*32,line);
        }
        if(p->status[0])panel_text(r,272,544,p->status);
        panel_text(r,272,604,"Up/down: part   A: replay   B: map");return;
    }
    if(p->kind==14){
        if(ui_canvas(&p->image)||ui_asset(b,"53_name.rmt",&p->name_artwork))return;
        ui_scale(&p->image,&b->layers[0],0,0,640,480);
        ui_blit(&p->image,&p->name_artwork,0,0,288,96,176,180,0);
        ui_fill(&p->image,187,207,266,24,0xffffff);ui_text(b,&p->image,p->name,190,211,260,20,0x18120a,0);
        ui_text(b,&p->image,"Y: 入力  X: 消去  A: 決定  B: 戻る",176,298,304,44,0xffffff,0);
        if(p->status[0])ui_text(b,&p->image,"日本語・英数字 16 文字以内",176,350,304,44,0xffda74,0);
        ui_present(r,&p->image,&p->texture);return;
    }
    if(p->kind==15){
        if(ui_canvas(&p->image))return;
        ui_scale(&p->image,&b->layers[0],0,0,640,480);ui_fill(&p->image,112,122,416,262,0xa08948);ui_fill(&p->image,115,125,410,256,0x291e14);
        ui_text(b,&p->image,"選択した履歴を消去しますか？",136,144,368,24,0xffffff,0);
        const char *labels[]={"既読履歴","選択肢履歴","シーンナビゲーション履歴","全履歴とセーブデータ"};unsigned row=0;
        for(unsigned i=0;i<4;i++)if(b->flag_dialog.checked[i]==1)ui_text(b,&p->image,labels[i],144,186+(int)row++*28,352,24,0xffdf78,0);
        ui_text(b,&p->image,"A: 消去       B: キャンセル",144,342,352,24,0xffffff,0);ui_present(r,&p->image,&p->texture);return;
    }
    if(p->kind==6||p->kind==18){
        if(!ui_canvas(&p->image)){ui_fill(&p->image,0,0,640,480,0x17120c);ui_fill(&p->image,120,160,400,164,0xa08948);ui_fill(&p->image,123,163,394,158,0x291e14);
            ui_text(b,&p->image,p->kind==6?"ゲームを終了しますか？":"タイトル画面に戻りますか？",152,190,352,32,0xffffff,0);
            ui_text(b,&p->image,"保存していない進行は失われます",144,234,352,32,0xe0c785,0);
            ui_text(b,&p->image,"A: はい       B: いいえ",168,282,320,24,0xffffff,0);ui_present(r,&p->image,&p->texture);}return;
    }
    if(p->kind==8){
        char title[80];snprintf(title,sizeof(title),"SETTINGS - PAGE %u/%u",p->selected/7+1,(bootstrap_setting_count()+6)/7);panel_text(r,272,108,title);
        unsigned first=p->selected/7*7;
        for(unsigned i=first;i<first+7&&i<bootstrap_setting_count();i++){
            char line[90],value[24];int current=bootstrap_message_setting(b,i,0),limit=bootstrap_setting_limit(i);
            if(limit==1)snprintf(value,sizeof(value),"%s",current?"On":"Off");
            else if(limit==2)snprintf(value,sizeof(value),"%s",current==0?"Normal":current==1?"Fast":"Instant");
            else snprintf(value,sizeof(value),"%d / %d",current,limit);
            snprintf(line,sizeof(line),"%s %s: %s",p->selected==i?">":" ",bootstrap_setting_label(i),value);panel_text(r,280,166+(int)(i-first)*48,line);
        }
        panel_text(r,272,528,"Up/down: select  Left/right: adjust  Y: page  B: back");
        panel_text(r,272,566,p->status[0]?p->status:"Applies to the next message, sound or visual effect.");return;
    }
    if(p->kind==13){
        panel_text(r,272,108,"NAWATSUNA / BONUS GAMES");unsigned first=p->selected/10*10;
        for(unsigned i=first;i<43&&i<first+10;i++){
            char line[100];if(i<40)snprintf(line,sizeof(line),"%s Scene %02u   %s",i==p->selected?">":" ",i+1,bootstrap_nawa_unlocked(b,i)?"Available":"Locked");
            else snprintf(line,sizeof(line),"%s Bonus game %u",i==p->selected?">":" ",i-39);
            panel_text(r,280,160+(int)(i%10)*36,line);
        }
        if(p->status[0])panel_text(r,272,560,p->status);
        panel_text(r,272,604,"D-pad: select / page   A: play   B: back");return;
    }
    if(p->kind==11){
        const char *names[]={"Anna","Maki","Suzune","Mika","Natsuko"};char line[100];
        snprintf(line,sizeof(line),"SCENE REPLAY - %s (%u / 5)",names[p->back],p->back+1);panel_text(r,240,36,line);
        for(unsigned i=0;i<bootstrap_replay_count(p->back);i++){
            const unsigned *xy=replay_positions[p->back<3?0:p->back-2][i];
            int x=160+(int)xy[0]*3/2,y=(int)xy[1]*3/2;
            snprintf(line,sizeof(line),"%s Scene %02u",i==p->selected?">":" ",i+1);panel_text(r,x,y,line);
            panel_text(r,x,y+24,bootstrap_replay_unlocked(b,p->back,i)?"Available":"Locked");
        }
        if(p->status[0])panel_text(r,272,554,p->status);
        panel_text(r,272,604,"D-pad: grid  X/Y: character  A: play  B: back");return;
    }
    if(p->kind==12){
        panel_text(r,272,160,"SELECT SCENE PLAYBACK");
        panel_text(r,280,260,p->selected==0?"> Animation":"  Animation");
        panel_text(r,280,320,p->selected==1?"> Still images":"  Still images");
        panel_text(r,272,528,"D-pad: select   A: confirm   B: cancel");return;
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
            SDL_SetRenderDrawColor(r,245,245,245,255);panel_text(r,208,696,bootstrap_gallery_scroll_extent(b,p->back,p->selected,p->variant)?"D-pad: pan   A/Y: zoom   X: scroll/skip   B: back":p->cg_zoom?"D-pad: pan   A/Y: fit   B: variants":"D-pad: variant   A/Y: full-size view   B: variants");return;
        }else{
            char title[100];snprintf(title,sizeof(title),"CG CATEGORY %u / 7%s",p->back+1,p->viewing?" - VARIANTS":"");panel_text(r,272,108,title);
            unsigned selected=p->viewing?p->variant:p->selected;
            unsigned count=p->viewing?gallery_variant_count(p,b):kgallery_group_count(&b->gallery,p->back)+(p->back==5?14:0);
            unsigned first=selected/10*10;
            for(unsigned i=first;i<count&&i<first+10;i++){
                unsigned item=p->viewing?p->selected:i,seen=0,total=kgallery_variant_count(&b->gallery,p->back,item);uint16_t images[3]={0};
                if(p->viewing){kgallery_variant(&b->gallery,p->back,item,i,images);seen=kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,images);}
                else for(unsigned v=0;v<total;v++){kgallery_variant(&b->gallery,p->back,item,v,images);seen+=kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,images);}
                char line[100];
                unsigned image_count=kgallery_group_count(&b->gallery,p->back);
                if(!p->viewing&&p->back==5&&item>=image_count)snprintf(line,sizeof(line),"%s Movie %02u  %s",i==selected?">":" ",item-image_count+1,bootstrap_gallery_movie_unlocked(b,item-image_count)?"Available":"Locked");
                else if(p->viewing)snprintf(line,sizeof(line),"%s Variant %03u  %s",i==selected?">":" ",i+1,seen?"Available":"Locked");
                else snprintf(line,sizeof(line),"%s CG %03u   %u / %u variants",i==selected?">":" ",i+1,seen,total);
                panel_text(r,280,160+(int)(i%10)*36,line);
            }
        }
        if(p->status[0])panel_text(r,272,560,p->status);
        panel_text(r,256,604,p->viewing?"D-pad: grid  L/R: page  A: view  B: back":"D-pad: grid  L/R: page  X/Y: category  A: open  B: back");return;
    }
    if(p->kind==9){
        panel_text(r,272,108,"MUSIC");unsigned first=p->selected/10*10;
        for(unsigned i=first;i<bootstrap_music_count()&&i<first+10;i++){char line[100];snprintf(line,sizeof(line),"%s %02u  %.55s",i==p->selected?">":" ",i+1,bootstrap_music_name(i));panel_text(r,280,160+(int)(i%10)*36,line);}
        panel_text(r,272,604,"D-pad: select   A / Enter: play   B / Backspace: back");return;
    }
    if(p->kind==4){
        if(!navigation_map_draw(p,b)){
            ui_fill(&p->image,0,0,640,24,0x1e1910);ui_fill(&p->image,0,452,640,28,0x1e1910);
            char label[80];snprintf(label,sizeof(label),"ナビマップ  SCENE %03u",b->scene->nodes[p->selected].id);ui_text(b,&p->image,label,12,3,616,20,0xffdf78,0);
            ui_text(b,&p->image,"上下左右: 移動  A: 場面選択  Y: 全体/拡大  B: 戻る",12,456,616,20,0xffffff,0);ui_present(r,&p->image,&p->texture);
        }return;
    }
    if(p->image.pixels&&(p->image.width!=640||p->image.height!=480)){rmt_free(&p->image);SDL_DestroyTexture(p->texture);p->texture=NULL;}
    if(b->history_count&&!bootstrap_history_draw(b,p->back,&p->image)){
        if(!p->texture)p->texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,640,480);
        if(p->texture){SDL_UpdateTexture(p->texture,NULL,p->image.pixels,(int)p->image.stride);SDL_Rect dst={224,80,832,544};SDL_RenderCopy(r,p->texture,NULL,&dst);}
    }
    char line[100];snprintf(line,sizeof(line),"HISTORY %u / %u",b->history_count?p->back+1:0,b->history_count);panel_text(r,272,108,line);
    if(p->status[0])panel_text(r,272,552,p->status);
    else panel_text(r,272,552,bootstrap_history_voice(b,p->back)?"A / Enter: replay voice":"No voice for this entry");
    panel_text(r,272,604,"Left: older   Right: newer   B / Backspace: back");
}

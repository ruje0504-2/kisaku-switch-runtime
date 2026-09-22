/* Executes the real startup script and presents the software display surface. */
#include "bootstrap.h"
#include <sys/stat.h>
#include <errno.h>
#include "image_sdl.h"
#include "save_slot.h"
#include <SDL_test_font.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "history_audio.h"
#include "switch_hos.h"
#ifdef __SWITCH__
#include <switch.h>
#endif
/* Keep portable panel text legible inside the 960x720 game viewport. */
static void panel_text(SDL_Renderer *r,int x,int y,const char *text){
    float sx,sy;SDL_RenderGetScale(r,&sx,&sy);
    SDL_RenderSetScale(r,sx*1.5f,sy*1.5f);
    while(*text){
        size_t n=strlen(text);if(n>60)n=60;
        char line[65];memcpy(line,text,n);line[n]=0;
        SDLTest_DrawString(r,(int)(x/1.5f),(int)(y/1.5f),line);
        text+=n;y+=20;
    }
    SDL_RenderSetScale(r,sx,sy);
}
#include "native_ui.inc"
#include "save_menu.inc"
#include "message_panel.inc"
#include "menu_touch.inc"
#include "scene_replay_menu.inc"
#include "cursor_panel.inc"
static void game_cancel(SaveMenu *m,MessagePanel *p,KBootstrap *b){
    if(b->scene_replay&&!b->letter_active){p->scene_cancel=1;return;}
    int was_hidden=b->message_user_hidden;
    int menu=b->message_active&&!b->letter_mode&&!b->letter_transition&&!b->message_slide&&!b->message_buttons_motion&&!b->area_active&&!b->choice_active&&!b->scene_replay;
    bootstrap_cancel(b);
    if(menu&&was_hidden){int mode=bootstrap_message_setting(b,20,0);if(mode)save_menu_open(m,b,mode==2);else{p->kind=8;p->setting_ready=0;}}
}
static int capture(SDL_Renderer *r,const char *path){
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_BGRA32);
    if(!s)return -1;
    int rc=SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_BGRA32,s->pixels,s->pitch);
    if(!rc)rc=SDL_SaveBMP(s,path);
    SDL_FreeSurface(s);return rc;
}
int main(int argc,char **argv){
    CursorPanel cursor={0};
    SceneReplayMenu navigation={0};unsigned restore_navigation_audio=0;
    MenuTouch touch={0};SaveMenu menu={0};MessagePanel panel={0};KHistoryAudio history_audio={0};unsigned history_serial=0;
    const char *root,*save_root,*shot=NULL;unsigned limit=0,new_game=0,start_story=0,advance_texts=0;
#ifdef __SWITCH__
    (void)argc;(void)argv;
    /* An installed NSP keeps the read-only game data in its RomFS and writable
       state in HOS SaveData; under hbmenu neither mount exists and both keep
       the SD-card layout below.  The buffers outlive the runtime. */
    static char switch_data[2048],switch_save[2048];
    snprintf(switch_data,sizeof(switch_data),"%s","sdmc:/switch/kisaku/game");
    snprintf(switch_save,sizeof(switch_save),"%s","sdmc:/switch/kisaku/saves");
    switch_hos_init(switch_data,sizeof(switch_data),switch_save,sizeof(switch_save));
    root=switch_data;save_root=switch_save;
#else
    if(argc<2){fprintf(stderr,"Usage: %s ELFIMAGE [--frames N] [--screenshot file.bmp]\n",argv[0]);return 2;}
    root=argv[1];save_root="local/saves";
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--start-story")){new_game=start_story=1;continue;}
        if(!strcmp(argv[i],"--new-game")){new_game=1;continue;}
        if(i+1>=argc)return 2;
        if(!strcmp(argv[i],"--frames")){char *end;long n=strtol(argv[++i],&end,10);if(*end||n<1||n>100000)return 2;limit=(unsigned)n;}
        else if(!strcmp(argv[i],"--advance-texts")){char *end;long n=strtol(argv[++i],&end,10);if(*end||n<1||n>100000)return 2;advance_texts=(unsigned)n;new_game=start_story=1;}
        else if(!strcmp(argv[i],"--screenshot"))shot=argv[++i];else return 2;
    }
#endif
    if(mkdir(save_root,0777)&&errno!=EEXIST){perror(save_root);return 1;}
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER))return 1;
    SDL_AudioDeviceID audio=0;unsigned serial=0,audio_rate=0,audio_channels=0;size_t audio_queued=0;
    int rc=1;KBootstrap *b=NULL;SDL_Texture *texture=NULL,*fade=NULL,*status_texture=NULL;
    SDL_Window *w=SDL_CreateWindow("KISAKU runtime preview",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,0);
    SDL_Renderer *r=w?SDL_CreateRenderer(w,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):NULL;
    if(!r&&w)r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);
    if(!r||SDL_RenderSetLogicalSize(r,1280,720))goto done;
    texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,640,480);
    fade=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,640,480);
    if(!texture||!fade)goto done;
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);SDL_SetTextureBlendMode(fade,SDL_BLENDMODE_BLEND);
    b=bootstrap_create_split(root,save_root);if(!b)goto done;
    char cursor_path[4096];snprintf(cursor_path,sizeof(cursor_path),"%s/kisaku-cursors.bin",root);
    if(cursor_load(&cursor,r,cursor_path)){
#ifndef __SWITCH__
        (void)cursor_load(&cursor,r,"local/native-cursors.bin");
#endif
    }
    if(bootstrap_enable_async_images(b)){fprintf(stderr,"Cannot start image decoding worker\n");goto done;}
    if(bootstrap_enable_async_voice(b)){fprintf(stderr,"Cannot start voice decoding worker\n");goto done;}
#ifdef __SWITCH__
    uint64_t ui_repeat_at=0,ui_held_previous=0;
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
#endif
    unsigned ticks=0,status_serial=~0u;int running=1,state=1;
    uint64_t frequency=SDL_GetPerformanceFrequency(),deadline=SDL_GetPerformanceCounter(),remainder=0;
    while(running){
#ifdef __SWITCH__
        if(!appletMainLoop())break;
        padUpdate(&pad);
        uint64_t buttons=padGetButtonsDown(&pad);
        if(navigation.phase==3&&(buttons&HidNpadButton_ZL))navigation.cancel=1;
        int opened_menu=0;
        if((buttons&HidNpadButton_L)&&!bootstrap_bowling_active(b)&&!panel.kind&&!menu.active&&!b->scene_replay){save_menu_open(&menu,b,0);opened_menu=1;}
        if(!panel.kind&&!menu.active&&b->message_active&&!b->choice_active&&!b->area_active&&!b->extra_active){
            if(navigation.phase!=3&&(buttons&HidNpadButton_ZL))bootstrap_message_action(b,0);
            if(buttons&HidNpadButton_ZR)bootstrap_message_action(b,1);
        }
        if(menu.active||panel.kind){
            uint64_t directions=HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right;
            uint64_t held=padGetButtons(&pad)&directions,now=SDL_GetTicks64();
            if(held!=ui_held_previous){ui_repeat_at=now+350;ui_held_previous=held;}
            else if(held&&now>=ui_repeat_at){buttons|=held;ui_repeat_at=now+90;}
        }else{ui_held_previous=0;}
        if(panel.kind){
            if((panel.kind==10||panel.kind==5||panel.kind==8||panel.kind==16)&&(buttons&HidNpadButton_L))message_panel_action(&panel,b,8);
            if((panel.kind==10||panel.kind==5||panel.kind==8||panel.kind==16)&&(buttons&HidNpadButton_R))message_panel_action(&panel,b,9);
            if((buttons&HidNpadButton_X)&&(panel.kind==4||panel.kind==10||panel.kind==11||panel.kind==8||panel.kind==14))message_panel_action(&panel,b,7);
            if(buttons&HidNpadButton_A)message_panel_action(&panel,b,0);
            if((buttons&HidNpadButton_Y)&&(panel.kind==5||panel.kind==16||panel.kind==14||panel.kind==4||panel.kind==10||panel.kind==8||panel.kind==9||panel.kind==11))message_panel_action(&panel,b,6);
            if(buttons&HidNpadButton_B)message_panel_action(&panel,b,1);
            if(buttons&HidNpadButton_Up)message_panel_action(&panel,b,2);
            if(buttons&HidNpadButton_Down)message_panel_action(&panel,b,3);
            if(buttons&HidNpadButton_Left)message_panel_action(&panel,b,4);
            if(buttons&HidNpadButton_Right)message_panel_action(&panel,b,5);
        }else if(menu.active){
            if((buttons&HidNpadButton_L)&&!opened_menu)save_menu_action(&menu,b,8);
            if(buttons&HidNpadButton_R)save_menu_action(&menu,b,9);
            if(buttons&HidNpadButton_A)save_menu_action(&menu,b,0);
            if(buttons&HidNpadButton_B)save_menu_action(&menu,b,1);
            if(buttons&HidNpadButton_Up)save_menu_action(&menu,b,2);
            if(buttons&HidNpadButton_Down)save_menu_action(&menu,b,3);
            if(buttons&HidNpadButton_Left)save_menu_action(&menu,b,4);
            if(buttons&HidNpadButton_Right)save_menu_action(&menu,b,5);
            if(buttons&HidNpadButton_Y)save_menu_action(&menu,b,6);
            if(buttons&HidNpadButton_X)save_menu_action(&menu,b,7);
        }else if(bootstrap_bowling_active(b)){
            HidAnalogStickState stick=padGetStickPos(&pad,0);
            KBowlingRuntime *r=b->bowling_runtime;
            int x=r->pointer_x+(abs(stick.x)>6000?stick.x/6000:0);
            int y=r->pointer_y-(abs(stick.y)>6000?stick.y/6000:0);
            x=x<0?0:x>639?639:x;y=y<0?0:y>479?479:y;
            if(r->game.phase==KB_GAME_TUTORIAL){
                if(buttons&HidNpadButton_Right)r->game.tutorial_page=1;
                if(buttons&HidNpadButton_Left)r->game.tutorial_page=0;
                if(buttons&HidNpadButton_A){r->focus=0;bootstrap_confirm(b);}
            }else if(r->game.phase==KB_GAME_RESULTS){if(buttons&HidNpadButton_A)bootstrap_confirm(b);}
            else bootstrap_bowling_pointer(b,x,y,(padGetButtons(&pad)&HidNpadButton_A)!=0);
            cursor.x=x;cursor.y=y;cursor.valid=1;
        }else{
        HidAnalogStickState left=padGetStickPos(&pad,0),right=padGetStickPos(&pad,1);
        cursor_sticks(&cursor,b,left.x,left.y,right.x,right.y);
        if(buttons&HidNpadButton_R)bootstrap_message_action(b,8);
        if((buttons&HidNpadButton_Up)&&b->message_active&&!b->choice_active) { bootstrap_message_action(b,5); }
        if(buttons&HidNpadButton_Y)bootstrap_message_action(b,0);
        if(buttons&HidNpadButton_X){bootstrap_message_action(b,7);b->message_hover=0;}
        if(buttons&HidNpadButton_A){if(b->message_active&&b->message_open&&b->message_hover>=0)bootstrap_message_action(b,(unsigned)b->message_hover);else bootstrap_confirm(b);}
        if(padGetButtonsDown(&pad)&HidNpadButton_Up)bootstrap_title_move(b,-1);
        if(padGetButtonsDown(&pad)&HidNpadButton_Down)bootstrap_title_move(b,1);
        if(padGetButtonsDown(&pad)&HidNpadButton_Left)bootstrap_menu_move(b,-1,0);
        if(padGetButtonsDown(&pad)&HidNpadButton_Right)bootstrap_menu_move(b,1,0);
        if(padGetButtonsDown(&pad)&HidNpadButton_B)game_cancel(&menu,&panel,b);
        }
#endif
        const Uint8 *keys=SDL_GetKeyboardState(NULL);
        if(panel.kind==8&&(keys[SDL_SCANCODE_LCTRL]||keys[SDL_SCANCODE_RCTRL]||keys[SDL_SCANCODE_LSHIFT]||keys[SDL_SCANCODE_RSHIFT]))config_motion_skip(&panel,b);
        b->force_skip=!panel.kind&&!menu.active&&(keys[SDL_SCANCODE_LCTRL]||keys[SDL_SCANCODE_RCTRL]);
        b->effect_fast=b->force_skip||(!panel.kind&&!menu.active&&(keys[SDL_SCANCODE_LSHIFT]||keys[SDL_SCANCODE_RSHIFT]));
        if(b->quit_requested)break;
        SDL_Event e;while(SDL_PollEvent(&e)){
            if(e.type==SDL_MOUSEMOTION){cursor.x=(e.motion.x-160)*640/960;cursor.y=e.motion.y*2/3;cursor.valid=1;cursor.auto_hidden=0;cursor.last_stick=SDL_GetTicks64();}
            if(e.type==SDL_MOUSEBUTTONDOWN){cursor.x=(e.button.x-160)*640/960;cursor.y=e.button.y*2/3;cursor.valid=1;if(cursor.auto_hidden)continue;
            }if(e.type==SDL_WINDOWEVENT){if(e.window.event==SDL_WINDOWEVENT_LEAVE||e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){cursor.valid=0;cursor.focused=0;panel.config_mouse_drag=0;kconfig_audio_release(&panel.config_audio);}if(e.window.event==SDL_WINDOWEVENT_ENTER||e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)cursor.focused=1;}if(e.type==SDL_QUIT)running=0;
            if(bootstrap_bowling_active(b)){
                KBowlingRuntime *r=b->bowling_runtime;
                if(e.type==SDL_MOUSEMOTION)bootstrap_bowling_pointer(b,cursor.x,cursor.y,(e.motion.state&SDL_BUTTON_LMASK)!=0);
                else if((e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEBUTTONUP)&&e.button.button==SDL_BUTTON_LEFT)
                    bootstrap_bowling_pointer(b,(e.button.x-160)*640/960,e.button.y*480/720,e.type==SDL_MOUSEBUTTONDOWN);
                else if(e.type==SDL_FINGERDOWN||e.type==SDL_FINGERMOTION||e.type==SDL_FINGERUP){
                    if(e.type==SDL_FINGERDOWN&&!touch.active){touch.active=1;touch.finger=e.tfinger.fingerId;touch.runtime=b;}
                    if(touch.active&&touch.runtime==b&&touch.finger==e.tfinger.fingerId){
                        bootstrap_bowling_pointer(b,(int)((e.tfinger.x*1280-160)*640/960),(int)(e.tfinger.y*480),e.type!=SDL_FINGERUP);
                        if(e.type==SDL_FINGERUP)touch.active=0;
                    }
                }else if(e.type==SDL_KEYDOWN){
                    SDL_Keycode key=e.key.keysym.sym;
                    if(r->game.phase==KB_GAME_TUTORIAL||r->game.phase==KB_GAME_RESULTS){
                        if(key==SDLK_LEFT)r->game.tutorial_page=0;
                        if(key==SDLK_RIGHT)r->game.tutorial_page=1;
                        if(key==SDLK_RETURN||key==SDLK_SPACE){r->focus=0;bootstrap_confirm(b);}
                    }else if(key==SDLK_SPACE)bootstrap_bowling_pointer(b,r->pointer_x,r->pointer_y,1);
                }else if(e.type==SDL_KEYUP&&e.key.keysym.sym==SDLK_SPACE)
                    bootstrap_bowling_pointer(b,r->pointer_x,r->pointer_y,0);
                continue;
            }
            if(menu_touch_event(&touch,&menu,&panel,b,&e,SDL_GetTicks64()))continue;
            if(menu.active&&menu.overwrite&&!panel.kind){
                ui_edit_text(menu.note,sizeof(menu.note),&e);
                if(e.type==SDL_MOUSEMOTION)save_menu_pointer(&menu,b,cursor.x,cursor.y,0);
                if(e.type==SDL_MOUSEBUTTONDOWN)save_menu_pointer(&menu,b,cursor.x,cursor.y,e.button.button==SDL_BUTTON_RIGHT?2:e.button.button==SDL_BUTTON_LEFT?1:0);
                if(e.type==SDL_KEYDOWN){if(e.key.keysym.sym==SDLK_RETURN)save_menu_action(&menu,b,0);else if(e.key.keysym.sym==SDLK_ESCAPE)save_menu_action(&menu,b,1);else if(e.key.keysym.sym==SDLK_LEFT)save_menu_action(&menu,b,4);else if(e.key.keysym.sym==SDLK_RIGHT)save_menu_action(&menu,b,5);}
                continue;
            }
            if(panel.kind==14){
                if(e.type==SDL_TEXTINPUT){size_t n=strlen(panel.name),add=strlen(e.text.text);if(n+add<sizeof(panel.name)){memcpy(panel.name+n,e.text.text,add+1);panel.status[0]=0;}}
                if(e.type==SDL_KEYDOWN){
                    if(e.key.keysym.sym==SDLK_RETURN)message_panel_action(&panel,b,0);
                    else if(e.key.keysym.sym==SDLK_ESCAPE){running=1;message_panel_action(&panel,b,1);}
                    else if(e.key.keysym.sym==SDLK_BACKSPACE||e.key.keysym.sym==SDLK_DELETE){size_t n=strlen(panel.name);if(n){n--;while(n&&((unsigned char)panel.name[n]&0xc0)==0x80)n--;panel.name[n]=0;}}
                }
                continue;
            }
            if(panel.kind){
                if(panel.kind==5){
                    if(e.type==SDL_MOUSEBUTTONDOWN){
                        if(e.button.button==SDL_BUTTON_RIGHT)backlog_pointer(&panel,b,cursor.x,cursor.y,2);
                        else if(e.button.button==SDL_BUTTON_LEFT)panel.backlog_mouse_drag=backlog_pointer(&panel,b,cursor.x,cursor.y,1);
                    }else if(e.type==SDL_MOUSEBUTTONUP)panel.backlog_mouse_drag=0;
                    else if(e.type==SDL_MOUSEMOTION){
                        if(panel.backlog_mouse_drag&&(e.motion.state&SDL_BUTTON_LMASK))backlog_drag(&panel,b,cursor.y);
                        else {panel.backlog_mouse_drag=0;backlog_pointer(&panel,b,cursor.x,cursor.y,0);}
                    }else if(e.type==SDL_MOUSEWHEEL){int dy=e.wheel.y;if(e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED)dy=-dy;if(dy)backlog_scroll(&panel,b,dy>0?1:-1,1);}
                }
                if(panel.kind==8){
                    if(e.type==SDL_MOUSEBUTTONDOWN){
                        if(e.button.button==SDL_BUTTON_RIGHT)settings_panel_action(&panel,b,1);
                        else if(e.button.button==SDL_BUTTON_LEFT)panel.config_mouse_drag=settings_panel_pointer(&panel,b,cursor.x,cursor.y,1);
                    }else if(e.type==SDL_MOUSEBUTTONUP){panel.config_mouse_drag=0;kconfig_audio_release(&panel.config_audio);}
                    else if(e.type==SDL_MOUSEMOTION){
                        if(panel.config_mouse_drag&&(e.motion.state&SDL_BUTTON_LMASK))config_drag(&panel,b,(int)panel.config_mouse_drag-1,cursor.x);
                        else {if(panel.config_mouse_drag)kconfig_audio_release(&panel.config_audio);panel.config_mouse_drag=0;settings_panel_pointer(&panel,b,cursor.x,cursor.y,0);}
                    }
                }
                if(panel.kind==6||panel.kind==18||panel.kind==19){
                    if(e.type==SDL_MOUSEMOTION||e.type==SDL_MOUSEBUTTONDOWN){
                        int click=e.type==SDL_MOUSEMOTION?0:e.button.button==SDL_BUTTON_LEFT?1:e.button.button==SDL_BUTTON_RIGHT?2:0;
                        message_dialog_pointer(&panel,b,cursor.x,cursor.y,click);
                    }
                }
                if(panel.kind==20&&(e.type==SDL_MOUSEMOTION||e.type==SDL_MOUSEBUTTONDOWN)){
                    int click=e.type==SDL_MOUSEMOTION?0:e.button.button==SDL_BUTTON_LEFT?1:e.button.button==SDL_BUTTON_RIGHT?2:0;
                    scene_mode_pointer(&panel,b,cursor.x,cursor.y,click);
                }
                if(e.type==SDL_KEYDOWN){SDL_Keycode key=e.key.keysym.sym;int action=key==SDLK_RETURN?0:(key==SDLK_BACKSPACE||key==SDLK_ESCAPE)?1:key==SDLK_UP?2:key==SDLK_DOWN?3:key==SDLK_LEFT?4:key==SDLK_RIGHT?5:key==SDLK_TAB?(panel.kind==4?1:6):key==SDLK_PAGEUP&&(panel.kind==10||panel.kind==5||panel.kind==8||panel.kind==16)?8:key==SDLK_PAGEDOWN&&(panel.kind==10||panel.kind==5||panel.kind==8||panel.kind==16)?9:(key==SDLK_r||key==SDLK_g)&&(panel.kind==4||panel.kind==10||panel.kind==8||panel.kind==11)?7:-1;if(action>=0)message_panel_action(&panel,b,action);}
                continue;
            }
            if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_s){save_menu_open(&menu,b,0);continue;}
            if(menu.active){
                if(e.type==SDL_MOUSEMOTION)save_menu_pointer(&menu,b,cursor.x,cursor.y,0);
                if(e.type==SDL_MOUSEBUTTONDOWN){if(e.button.button==SDL_BUTTON_LEFT)save_menu_pointer(&menu,b,cursor.x,cursor.y,1);else if(e.button.button==SDL_BUTTON_RIGHT)save_menu_action(&menu,b,1);}
                if(e.type==SDL_MOUSEWHEEL)save_menu_action(&menu,b,e.wheel.y>0?2:3);
                if(e.type==SDL_KEYDOWN){
                    SDL_Keycode key=e.key.keysym.sym;
                    if(menu.param_detail&&(key==SDLK_LSHIFT||key==SDLK_RSHIFT||key==SDLK_LCTRL||key==SDLK_RCTRL)){save_menu_param_skip(&menu);continue;}
                    int action=key==SDLK_RETURN?0:(key==SDLK_BACKSPACE||key==SDLK_ESCAPE)?1:key==SDLK_UP?2:key==SDLK_DOWN?3:key==SDLK_LEFT?4:key==SDLK_RIGHT?5:key==SDLK_TAB?(panel.kind==4?1:6):key==SDLK_g?7:key==SDLK_PAGEUP?8:key==SDLK_PAGEDOWN?9:-1;
                    if(action>=0)save_menu_action(&menu,b,action);
                }
                continue;
            }
            if(e.type==SDL_KEYDOWN){
                if(e.key.keysym.sym==SDLK_r)bootstrap_message_action(b,8);
                if(e.key.keysym.sym==SDLK_l)bootstrap_message_action(b,5);
                if(e.key.keysym.sym==SDLK_y)bootstrap_message_action(b,0);
                if(e.key.keysym.sym==SDLK_x){bootstrap_message_action(b,7);b->message_hover=0;}
                if(e.key.keysym.sym==SDLK_RETURN||e.key.keysym.sym==SDLK_SPACE){if(b->message_active&&b->message_open&&b->message_hover>=0)bootstrap_message_action(b,(unsigned)b->message_hover);else bootstrap_confirm(b);}
                if(e.key.keysym.sym==SDLK_LEFT)bootstrap_menu_move(b,-1,0);
                if(e.key.keysym.sym==SDLK_RIGHT)bootstrap_menu_move(b,1,0);
                if(e.key.keysym.sym==SDLK_ESCAPE||e.key.keysym.sym==SDLK_DELETE)game_cancel(&menu,&panel,b);
                if(e.key.keysym.sym==SDLK_BACKSPACE&&b->message_active){bootstrap_message_action(b,5);}
                if(e.key.keysym.sym==SDLK_TAB)bootstrap_message_action(b,4);
                if(e.key.keysym.sym==SDLK_UP)bootstrap_title_move(b,-1);
                if(e.key.keysym.sym==SDLK_DOWN)bootstrap_title_move(b,1);
            }
            if(e.type==SDL_MOUSEWHEEL&&b->message_active){if(e.wheel.y>0){bootstrap_message_action(b,5);}else if(e.wheel.y<0)bootstrap_confirm(b);}

            if(e.type==SDL_MOUSEMOTION){cursor.x=e.motion.x<160?-1:(e.motion.x-160)*640/960;cursor.y=e.motion.y<0?-1:e.motion.y*480/720;cursor.valid=1;bootstrap_pointer(b,cursor.x,cursor.y,0);}
            if(e.type==SDL_MOUSEBUTTONDOWN){cursor.x=e.button.x<160?-1:(e.button.x-160)*640/960;cursor.y=e.button.y<0?-1:e.button.y*480/720;cursor.valid=1;if(e.button.button==SDL_BUTTON_RIGHT){game_cancel(&menu,&panel,b);continue;}if(e.button.button!=SDL_BUTTON_LEFT)continue;if(b->area_active||b->choice_active||b->title.active||b->flag_dialog.active||b->message_active)bootstrap_pointer(b,cursor.x,cursor.y,1);else bootstrap_confirm(b);}
        }
        if(navigation.phase==3&&SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F8])navigation.cancel=1;
        if(bootstrap_bowling_active(b)&&b->bowling_runtime->game.phase==KB_GAME_TURN){
            KBowlingRuntime *r=b->bowling_runtime;
            int dx=(keys[SDL_SCANCODE_RIGHT]-keys[SDL_SCANCODE_LEFT])*5;
            int dy=(keys[SDL_SCANCODE_DOWN]-keys[SDL_SCANCODE_UP])*5;
            if(dx||dy){
                int x=r->pointer_x+dx,y=r->pointer_y+dy;
                x=x<0?0:x>639?639:x;y=y<0?0:y>479?479:y;
                bootstrap_bowling_pointer(b,x,y,keys[SDL_SCANCODE_SPACE]!=0);
            }
        }
        if(b->scene_panel_request){
            int request=b->scene_panel_request;b->scene_panel_request=0;
            if(request<0&&(panel.kind==4||panel.kind==16))panel.kind=0;
            if(request==1){panel.kind=4;panel.nav_catalog_ready=0;panel.selected=panel.back=panel.viewing=0;panel.status[0]=0;}
            if(request==3){panel.kind=20;panel.direct_scene=1;panel.direct_count=0;panel.direct_selected=0;panel.direct_page=0;panel.status[0]=0;}
            if(request>0&&b->scene&&panel.kind==4){
                for(unsigned i=0;i<b->scene->count;i++)if(b->scene->nodes[i].id==b->scene_focus&&b->scene->nodes[i].width){panel.selected=i;break;}
                panel.back=0;
            }
        }
        if(b->extra_request){panel.kind=b->extra_request;panel.selected=panel.back=panel.viewing=panel.variant=0;panel.status[0]=0;b->extra_request=0;if(panel.kind==14){snprintf(panel.name,sizeof(panel.name),"会員１号");SDL_StartTextInput();}}
        if(panel.dismiss_menus){menu.active=0;b->load_modal=b->file_modal=0;panel.dismiss_menus=0;bootstrap_message_hide(b,0);}
        if(b->message_request){
            unsigned action=b->message_request;b->message_request=0;
            if(action==2||action==3)save_menu_open(&menu,b,action==3);
            else{panel.kind=action;if(action==4)panel.nav_catalog_ready=0;panel.selected=panel.back=panel.viewing=panel.variant=0;panel.status[0]=0;}
        }
        if(menu.request){panel.kind=(unsigned)menu.request;panel.selected=panel.back=panel.viewing=0;panel.status[0]=0;menu.request=0;}
        if(panel.nav_load_request){
            unsigned slot=panel.nav_load_request;panel.nav_load_request=0;menu.active=1;menu.save=0;menu.selector=0;menu.slot=(int)slot;menu.focus=0;menu.overwrite=0;
            save_menu_action(&menu,b,0);if(menu.loading){panel.kind=0;b->scene_modal=0;}else{menu.active=0;snprintf(panel.status,sizeof(panel.status),"ロードできませんでした");}
        }
        int navigation_change=scene_replay_step(&navigation,&b,&panel);
        if(navigation_change==1){
            navigation.queued=audio_queued;navigation.sound=history_audio;
            if(audio&&khistory_audio_suspend(&navigation.sound,audio))goto done;
            serial=~b->audio_serial;audio_queued=0;status_serial=~0u;state=1;
        }else if(navigation_change==2){
            serial=~b->audio_serial;audio_queued=navigation.queued;status_serial=~0u;state=1;restore_navigation_audio=1;
        }
        if(b->title_load_requested){b->title_load_requested=0;save_menu_open(&menu,b,1);}
        if(panel.animation_request){
            panel.animation_request=0;menu.next=bootstrap_create_split(b->root,b->save_root);
            if(menu.next&&!menu.next->error[0]&&!bootstrap_enable_async_voice(menu.next)&&!bootstrap_enable_async_images(menu.next)){menu.loading=4;menu.active=1;menu.selector=b->vm->bytes[8100]>3?3:b->vm->bytes[8100];menu.ticks=0;snprintf(menu.status,sizeof(menu.status),"表示を切り替えています…");}
            else{bootstrap_destroy(menu.next);menu.next=NULL;menu.active=1;snprintf(menu.status,sizeof(menu.status),"表示の切替に失敗しました");}
        }
        if(panel.return_title){
            panel.return_title=0;
            if(!bootstrap_flush_progress(b)){menu.next=bootstrap_create_split(b->root,b->save_root);
                if(menu.next&&!menu.next->error[0]&&!bootstrap_enable_async_voice(menu.next)&&!bootstrap_enable_async_images(menu.next)){menu.loading=3;menu.active=1;menu.ticks=0;snprintf(menu.status,sizeof(menu.status),"Returning to title...");}
                else{bootstrap_destroy(menu.next);menu.next=NULL;menu.active=1;snprintf(menu.status,sizeof(menu.status),"Cannot initialize title");}
            }else{menu.active=1;snprintf(menu.status,sizeof(menu.status),"Could not preserve progress");}
        }
        if(save_menu_step(&menu,&b)){state=1;serial=~b->audio_serial;audio_queued=0;status_serial=~0u;}

        if(audio&&(menu.active||(panel.kind&&((panel.kind<9&&panel.kind!=5)||panel.kind>=16))||(panel.kind==5&&!history_audio.suspended)))SDL_PauseAudioDevice(audio,1);
        if(new_game&&b->title.active&&b->title.age>=64){bootstrap_title_move(b,1);bootstrap_confirm(b);new_game=0;}
        if(start_story&&b->flag_dialog.active){bootstrap_pointer(b,300,350,1);start_story=0;}
        if(advance_texts&&b->text_count<advance_texts&&b->message_active&&!b->message_slide)bootstrap_confirm(b);
        if(state==1&&!menu.active&&(!panel.kind||(panel.kind>=9&&panel.kind<=15))){bootstrap_frame(b);state=bootstrap_run(b,100000);if(state<0)fprintf(stderr,"%s\n",b->error);}
        if(b->audio_serial!=serial){
            memset(&history_audio,0,sizeof(history_audio));
            if(audio&&(audio_rate!=b->audio_rate||audio_channels!=b->audio_channels)){SDL_CloseAudioDevice(audio);audio=0;}
            if(!audio){
                SDL_AudioSpec spec={0};spec.freq=(int)b->audio_rate;spec.format=AUDIO_S16LSB;spec.channels=(Uint8)b->audio_channels;spec.samples=1024;
                audio=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);if(!audio)goto done;
                audio_rate=b->audio_rate;audio_channels=b->audio_channels;
            }else SDL_ClearQueuedAudio(audio);
            audio_queued=0;serial=b->audio_serial;
        }
        if(settings_panel_audio(&panel,&history_audio,audio,audio_rate,audio_channels))goto done;
        if(history_voice_audio(&panel,&history_audio,audio,audio_rate,audio_channels,&history_serial))goto done;
        if(restore_navigation_audio&&audio){
            history_audio=navigation.sound;if(khistory_audio_restore(&history_audio,audio))goto done;
            audio_queued=navigation.queued;restore_navigation_audio=0;
        }
        if(audio&&!menu.active&&(!panel.kind||(panel.kind>=9&&panel.kind<=15))){
            uint8_t chunk[4096];
            while(SDL_GetQueuedAudioSize(audio)<8192){
                size_t n=bootstrap_audio_mix_read(b,&audio_queued,chunk,sizeof(chunk));if(!n)break;
                if(khistory_audio_queue(&history_audio,audio,chunk,n))goto done;
            }
        }
        if(audio&&!menu.active&&(!panel.kind||(panel.kind>=9&&panel.kind<=15)))SDL_PauseAudioDevice(audio,0);
        SDL_SetRenderDrawColor(r,12,15,20,255);SDL_RenderClear(r);
        SDL_Rect dst={160,0,960,720};
        if(b->layers[0].pixels){
            if(SDL_UpdateTexture(texture,NULL,b->layers[0].pixels,(int)b->layers[0].stride))goto done;
            SDL_RenderCopy(r,texture,NULL,&dst);
        }
        if(b->fade_visible&&b->fade_surface.pixels){
            if(SDL_UpdateTexture(fade,NULL,b->fade_surface.pixels,(int)b->fade_surface.stride))goto done;
            SDL_SetTextureAlphaMod(fade,(Uint8)b->fade_alpha);SDL_RenderCopy(r,fade,NULL,&dst);
        }
        if(b->status_visible&&b->status_image.pixels){
            if(!status_texture||status_serial!=b->status_serial){SDL_DestroyTexture(status_texture);status_texture=kimage_texture(r,&b->status_image);status_serial=b->status_serial;}
            if(status_texture){SDL_Rect meter={8,24,145,300};SDL_RenderCopy(r,status_texture,NULL,&meter);}
        }
        if(b->area_active){
            SDL_SetRenderDrawColor(r,255,235,96,255);
            if(!cursor.texture&&b->area_selected>=0){int x=160+b->area_x*3/2,y=b->area_y*3/2;SDL_RenderDrawLine(r,x-9,y,x+9,y);SDL_RenderDrawLine(r,x,y-9,x,y+9);}
            SDL_Rect hint={208,674,864,34};SDL_SetRenderDrawColor(r,16,20,28,230);SDL_RenderFillRect(r,&hint);SDL_SetRenderDrawColor(r,240,240,240,255);
            if(b->area_active==2){char line[96];snprintf(line,sizeof(line),"D-pad/sticks: target  A: use  B: tool (%u / %u)",b->vm->bytes[1950]+1,b->vm->bytes[1951]);panel_text(r,224,684,line);}else panel_text(r,224,684,"D-pad/sticks: target  A: inspect  B: return");
        }
        if(b->bonus52_active==1){SDL_SetRenderDrawColor(r,240,240,240,255);panel_text(r,224,684,"Move either stick or press A repeatedly to fill the meter.");}
        if(state<0||getenv("KISAKU_DEBUG_OVERLAY")){
        SDL_SetRenderDrawColor(r,230,235,245,255);
        SDLTest_DrawString(r,24,640,"KISAKU / AI6WIN - STARTUP PREVIEW - PORT IN PROGRESS");
        char line[160];snprintf(line,sizeof(line),"Calls: %u | Frame: %llu | %s",b->handled,(unsigned long long)b->frames,state<0?"UNSUPPORTED CALL":state==1?(b->choice_active?"CHOICE / UP-DOWN + A or Enter":b->message_active?"STORY / A or Enter: advance":b->flag_dialog.active?"NEW GAME / D-PAD + A or Enter":b->title.active?"TITLE / UP-DOWN + A or Enter":"ANIMATING"):"SCRIPT YIELDED");
        SDLTest_DrawString(r,24,662,line);
        if(state<0){snprintf(line,sizeof(line),"%.150s",b->error);SDLTest_DrawString(r,24,684,line);}
        }
        if(navigation.phase==3){SDL_SetRenderDrawColor(r,245,245,245,255);panel_text(r,224,692,"SCENE REPLAY - ZL / F8: return to story");}
        if(menu.active)save_menu_draw(&menu,b,r);
        if(panel.kind)message_panel_draw(&panel,b,r);
        if(limit&&++ticks>=limit){if(shot&&capture(r,shot))goto done;running=0;}
        if(cursor_draw(&cursor,b,r,menu.active||(panel.kind&&!b->bonus52_active),SDL_GetTicks64()))goto done;
        SDL_RenderPresent(r);
        remainder+=frequency;deadline+=remainder/60;remainder%=60;
        uint64_t now=SDL_GetPerformanceCounter();
        if(now<deadline){uint64_t ms=(deadline-now)*1000/frequency;if(ms)SDL_Delay((Uint32)ms);}
        else if(now-deadline>frequency/4)deadline=now;
    }
    rc=(state<0||bootstrap_flush_progress(b))?1:0;
    if(state<0){char report[4096];snprintf(report,sizeof(report),"%s/runtime-error.txt",save_root);FILE *f=fopen(report,"w");if(f){fprintf(f,"%s\n",b->error);fclose(f);}fprintf(stderr,"%s\n",b->error);}
 done:
    if(rc&&(!b||!b->error[0]))fprintf(stderr,"Runtime viewer: %s\n",SDL_GetError());
    save_menu_clear(&menu);
    cursor_clear(&cursor);
    history_voice_stop(&panel);kvoice_worker_destroy(panel.voice_worker);
    kconfig_audio_clear(&panel.config_audio);
    gallery_panel_clear(&panel);nawa_panel_clear(&panel);
    rmt_free(&panel.settings_artwork);rmt_free(&panel.sidebar_artwork);
    for(unsigned i=0;i<5;i++)rmt_free(&panel.config_art[i]);
    rmt_free(&panel.dialog_artwork);rmt_free(&panel.dialog_body);
    rmt_free(&panel.name_artwork);rmt_free(&panel.nav_artwork);rmt_free(&panel.nav_scene);for(unsigned i=0;i<4;i++)rmt_free(&panel.nav_previews[i]);rmt_free(&panel.history_artwork);
    rmt_free(&panel.direct_artwork);rmt_free(&panel.direct_parts);
    rmt_free(&panel.image);SDL_DestroyTexture(panel.texture);SDL_DestroyTexture(status_texture);
    if(audio)SDL_CloseAudioDevice(audio);
    bootstrap_destroy(navigation.next);bootstrap_destroy(navigation.owner);bootstrap_destroy(menu.next);bootstrap_destroy(b);SDL_DestroyTexture(fade);SDL_DestroyTexture(texture);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return rc;
}

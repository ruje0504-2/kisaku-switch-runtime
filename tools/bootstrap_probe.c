#include "bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
int main(int argc,char **argv){
    int new_game=0;unsigned choice_visits=0,frame_limit=10000;const char *choice_policy="explore";
    unsigned choice_sequence[4096]={0},choice_sequence_count=0;
    struct {int module; size_t ip; unsigned visits;} menus[2048]={0};
    unsigned menu_count=0;
    const char *sequence_text=getenv("KISAKU_CHOICE_SEQUENCE");
    if(sequence_text&&*sequence_text){
        const char *p=sequence_text;while(*p&&choice_sequence_count<4096){
            char *end=NULL;unsigned long n=strtoul(p,&end,10);
            if(end==p||n>255){choice_sequence_count=0;break;}
            choice_sequence[choice_sequence_count++]=(unsigned)n;
            if(!*end)break;
            if(*end!=','){choice_sequence_count=0;break;}
            p=end+1;
        }
    }
#ifdef __SWITCH__
    (void)argc;(void)argv;consoleInit(NULL);const char *root="sdmc:/switch/kisaku/game";
#else
    if(argc<2){fprintf(stderr,"Usage: %s ELFIMAGE [--new-game] [--frames N]\n",argv[0]);return 2;}const char *root=argv[1];
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--new-game"))new_game=1;
        else if(!strcmp(argv[i],"--frames")&&i+1<argc){char *end;unsigned long n=strtoul(argv[++i],&end,10);if(*end||!n||n>1000000)return 2;frame_limit=(unsigned)n;}
        else if(!strcmp(argv[i],"--choice")&&i+1<argc){choice_policy=argv[++i];if(strcmp(choice_policy,"first")&&strcmp(choice_policy,"last")&&strcmp(choice_policy,"cycle")&&strcmp(choice_policy,"explore"))return 2;}
        else return 2;
    }
#endif
    #ifdef __SWITCH__
    const char *saves="sdmc:/switch/kisaku/saves";
#else
    const char *saves=getenv("KISAKU_SAVE_DIR");if(!saves)saves="local/saves";
#endif
    if(mkdir(saves,0777)&&errno!=EEXIST){perror(saves);return 1;}
    KBootstrap *b=bootstrap_create_split(root,saves);if(!b)return 1;
    int trace_module=-1;const char *trace=getenv("KISAKU_TRACE_MODULE");
#define TRACE_MODULE() do{if(trace&&b->vm->module!=trace_module){trace_module=b->vm->module;printf("Module: %s @0x%zx\n",trace_module>=0?b->vm->modules[trace_module].name:"<none>",b->vm->ip);}}while(0)
    int result=bootstrap_run(b,100000);
    TRACE_MODULE();
    for(unsigned frame=0;result==1&&frame<10000&&!(b->title.active&&b->title.age>=64);frame++){bootstrap_frame(b);result=bootstrap_run(b,100000);TRACE_MODULE();}
    if(new_game&&b->title.active&&b->title.age>=64){
        unsigned title_index=0;const char *title_env=getenv("KISAKU_TITLE_INDEX");
        if(title_env&&*title_env)title_index=(unsigned)strtoul(title_env,NULL,10);
        for(unsigned i=0;i<=title_index;i++)bootstrap_title_move(b,1);
        bootstrap_confirm(b);result=bootstrap_run(b,100000);
        for(unsigned frame=0;result==1&&frame<frame_limit;frame++){if(b->flag_dialog.active){bootstrap_pointer(b,300,350,1);}if(b->extra_active&&b->extra_kind==14){bootstrap_name_submit(b,"鬼作");}if(b->file_modal==2){/* 31/910 character status: probe closes the read-only modal. */b->character_request=0;b->file_modal=0;b->vm->globals[0][18]=(KValue){0,NULL};}else if(b->file_modal||b->load_modal){if(getenv("KISAKU_AUTO_SAVE")){int can=bootstrap_can_save(b),saved=can?bootstrap_save_slot(b,98):-99;fprintf(stderr,"AUTO file=%u load=%u request=%u can=%d save=%d sp=%u ip=0x%zx b0_18=%d b1_60=%d exec=0x%x msg=%u hidden=%u open=%u wait=%u/%llu scripts=%u depth=%u module=%s\n",b->file_modal,b->load_modal,b->message_request,can,saved,b->vm->sp,b->vm->ip,b->vm->globals[0][18].number,b->vm->globals[1][60].number,b->exec_status,b->message_active,b->message_user_hidden,b->message_open,b->wait_input,(unsigned long long)b->wait_clock,b->vm->script_depth,b->vm->depth,b->vm->modules[b->vm->module].name);}unsigned modal=b->file_modal;b->file_modal=b->load_modal=0;b->message_request=0;if(modal==3)b->exec_status&=~16u;else if(modal==1)b->exec_status&=~8u;bootstrap_message_hide(b,0);}if(b->choice_active){ 
                KList *l=&b->vm->lists[b->vm->current_list];
                unsigned key=0;
                while(key<menu_count&&(menus[key].module!=l->items[0].module||menus[key].ip!=l->items[0].ip))key++;
                if(key==menu_count){if(menu_count==2048){result=-1;break;}menus[key].module=l->items[0].module;menus[key].ip=l->items[0].ip;menu_count++;}
                unsigned visit=menus[key].visits++,select=0;
                choice_visits++;
                if(choice_sequence_count&&choice_visits<=choice_sequence_count)select=choice_sequence[choice_visits-1];
                else if(!strcmp(choice_policy,"last"))select=b->choice_count-1;
                else if(!strcmp(choice_policy,"cycle")||!strcmp(choice_policy,"explore"))select=visit%b->choice_count;
                /* Read-history bytes are colours, not disabled options in story mode. */
                if(select>=b->choice_count){fprintf(stderr,"Invalid route choice %u at visit %u\n",select,choice_visits);result=-1;break;}
                if(getenv("KISAKU_TRACE_CHOICES")){
                    printf("Choice %u menu=%s@%zx visit=%u selected=%u count=%u cancel=%u values=",choice_visits,b->vm->modules[l->items[0].module].name,l->items[0].ip,visit,select,b->choice_count,b->vm->bytes[1500]);
                    for(unsigned j=0;j<b->choice_count;j++)printf(" %d/%d",b->choice_values[j],b->choice_returns[j]);
                    puts("");fflush(stdout);
                }
                /* A one-item normal menu with byte 1500 set is a
                   cancel-capable parent menu.  The native controller lets
                   B leave it; selecting its only row re-enters the child
                   event forever and does not represent route progress. */
                if(b->choice_normal&&b->vm->bytes[1500]&&
                   (visit>=b->choice_count||b->choice_count==1)){
                    if(getenv("KISAKU_TRACE_CHOICES"))printf("Choice cancel menu=%s@%zx\n",b->vm->modules[l->items[0].module].name,l->items[0].ip);
                    bootstrap_cancel(b);
                }else{b->choice_selected=(int)select;bootstrap_confirm(b);}
            }else if((b->message_active&&!b->message_slide)||b->wait_input)bootstrap_confirm(b);bootstrap_frame(b);result=bootstrap_run(b,100000);TRACE_MODULE();if(b->title.active&&b->title.age>=64)break;}
    }
    if(result==1&&!b->title.active)printf("Pending: %s @0x%zx syscall=%d sub=%d opcode=%u wait=%llu native_wait=%llu input=%u message=%u choice=%u area=%u extra=%u fade=%u param=%u exec=%u replay=%u ax=%u cg=%u bowling=%u video=%u image=%u scroll=%u blink=%u novel=%u transition=%u wipe=%u helper=%u exec522=%u fadeMes=%u letter=%u stack=%u depth=%u scripts=%u\n",b->vm->modules[b->vm->module].name,b->vm->instruction_ip,b->vm->syscall,b->vm->sp?b->vm->stack[b->vm->sp-1].number:-1,b->vm->opcode,(unsigned long long)b->wait_clock,(unsigned long long)b->native_wait_clock,b->wait_input,b->message_active,b->choice_active,b->area_active,b->extra_active,b->fade_steps,b->param_animation_active,b->exec523_active,b->scene_replay_finished,b->ax_modal,b->native_cg!=NULL,bootstrap_bowling_active(b),b->video_wait,b->image_loading,b->scroll_active,b->blink_active,b->novel_transition,b->transition_steps,b->exec_wipe_active,b->helper_steps,b->exec522_motion,b->mes_fade_transition,b->letter_transition,b->vm->sp,b->vm->depth,b->vm->script_depth);
    int title_ready=result==1&&b->title.active&&b->title.age>=64&&!b->error[0];
    printf("KISAKU startup diagnostic - NOT GAMEPLAY\nHandled calls: %u\nLayers: %u\nLast image layer: %d\nRead flag bytes: %zu\n",b->handled,b->layer_count,b->last_loaded_layer,b->read_size);
    printf("Frames: %llu | System flags: 0x%x | Settings: %u | FLAG snapshots: %u\n",(unsigned long long)b->frames,(unsigned)b->vm->globals[0][50].number,b->setting_count,b->flag_file_count);
    if(trace)printf("Route bytes: 0x3f6=%u 0x3f7=%u 0x3f8=%u 0x3f9=%u 0x3fa=%u 0x3fb=%u 0x3fc=%u 0x3fd=%u\n",b->vm->bytes[0x3f6],b->vm->bytes[0x3f7],b->vm->bytes[0x3f8],b->vm->bytes[0x3f9],b->vm->bytes[0x3fa],b->vm->bytes[0x3fb],b->vm->bytes[0x3fc],b->vm->bytes[0x3fd]);
    printf("Operand stack: %u values, capacity %u | Choice visits: %u | Logo: phase=%u audio=%zu/%zu ax=%u\n",b->vm->sp,b->vm->stack_capacity,choice_visits,b->logo_phase,b->audio_cursor,b->audio_size,b->ax.size);
    printf("Param animation: active=%u phase=%u step=%u/%u tracks=%u,%u events=%u,%u values=%d,%d,%d,%d total=%u\n",
        b->param_animation_active,b->param_animation_phase,b->param_animation_step,b->param_animation_plan.steps,
        b->ax.cells[0].state,b->ax.cells[2].state,b->ax_events[0],b->ax_events[2],b->param_values[0],b->param_values[1],b->param_values[2],b->param_values[3],b->param_total);
    unsigned funcs=0;for(unsigned i=0;i<1024;i++)funcs+=b->vm->functions[i].valid;
    printf("Registered functions: %u | Texts: %u\n%s\n",funcs,b->text_count,title_ready?"Title menu ready (gameplay incomplete)":result<0?b->error:result==1?"Frame budget exhausted with runtime work pending (not a pass)":"Script yielded");
    if(result<0&&b->vm->current_list>=0)printf("Current list: %d, items=%u\n",b->vm->lists[b->vm->current_list].id,b->vm->lists[b->vm->current_list].count);
    if(result<0){printf("AX: %s | context: %s\nStack:",b->loaded_animation,b->animation_name);for(unsigned i=0;i<b->vm->sp;i++){KValue v=b->vm->stack[i];if(v.string)printf(" [%s]",v.string);else printf(" %d",v.number);}puts("");}
    /* Retain an on-device result even when the emulator cannot render its console. */
#ifdef __SWITCH__
    FILE *report=fopen("sdmc:/switch/kisaku/bootstrap-result.txt","w");
    if(report){fprintf(report,"result=%d\ntitle_ready=%d\nframes=%llu\ncalls=%u\ntexts=%u\nerror=%s\n",result,title_ready,(unsigned long long)b->frames,b->handled,b->text_count,b->error);fclose(report);}
#endif
    bootstrap_destroy(b);
#ifdef __SWITCH__
    
    puts("Use the HOME menu to close.");while(appletMainLoop()){consoleUpdate(NULL);}consoleExit(NULL);
#endif
    return result&&!title_ready?1:0;
}

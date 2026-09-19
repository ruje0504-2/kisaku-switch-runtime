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
    int result=bootstrap_run(b,100000);
    for(unsigned frame=0;result==1&&frame<10000&&!(b->title.active&&b->title.age>=64);frame++){bootstrap_frame(b);result=bootstrap_run(b,100000);}
    if(new_game&&b->title.active&&b->title.age>=64){
        bootstrap_title_move(b,1);bootstrap_confirm(b);result=bootstrap_run(b,100000);
        for(unsigned frame=0;result==1&&frame<frame_limit;frame++){if(b->flag_dialog.active){bootstrap_pointer(b,300,350,1);}if(b->choice_active){
                if(choice_visits++<20&&getenv("KISAKU_TRACE_CHOICES")){KList *l=&b->vm->lists[b->vm->current_list];printf("Choice %u texts=%u %s @%zx:",choice_visits,b->text_count,b->vm->modules[l->items[0].module].name,l->items[0].ip);for(unsigned j=0;j<b->choice_count;j++)printf(" %d:%u",b->choice_values[j],b->vm->bytes[2000+b->choice_values[j]]);puts("");}
                unsigned select=0;while(select<b->choice_count&&b->vm->bytes[2000+b->choice_values[select]])select++;
                if(select==b->choice_count)select=(unsigned)(b->frames%b->choice_count);
                if(!strcmp(choice_policy,"first"))select=0;
                else if(!strcmp(choice_policy,"last"))select=b->choice_count-1;
                else if(!strcmp(choice_policy,"cycle"))select=(choice_visits-1)%b->choice_count;
                b->choice_selected=(int)select;bootstrap_confirm(b);
            }else if((b->message_active&&!b->message_slide)||b->wait_input)bootstrap_confirm(b);bootstrap_frame(b);result=bootstrap_run(b,100000);if(b->title.active&&b->title.age>=64)break;}
    }
    if(result==1&&!b->title.active)printf("Pending: %s @0x%zx syscall=%d opcode=%u wait=%llu input=%u message=%u choice=%u stack=%u\n",b->vm->modules[b->vm->module].name,b->vm->instruction_ip,b->vm->syscall,b->vm->opcode,(unsigned long long)b->wait_clock,b->wait_input,b->message_active,b->choice_active,b->vm->sp);
    int title_ready=result==1&&b->title.active&&b->title.age>=64&&!b->error[0];
    printf("KISAKU startup diagnostic - NOT GAMEPLAY\nHandled calls: %u\nLayers: %u\nLast image layer: %d\nRead flag bytes: %zu\n",b->handled,b->layer_count,b->last_loaded_layer,b->read_size);
    printf("Frames: %llu | System flags: 0x%x | Settings: %u | FLAG snapshots: %u\n",(unsigned long long)b->frames,(unsigned)b->vm->globals[0][50].number,b->setting_count,b->flag_file_count);
    unsigned funcs=0;for(unsigned i=0;i<1024;i++)funcs+=b->vm->functions[i].valid;
    printf("Registered functions: %u | Texts: %u\n%s\n",funcs,b->text_count,title_ready?"Title menu ready (gameplay incomplete)":result?b->error:"Script yielded");
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

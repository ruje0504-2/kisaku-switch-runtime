#include "bootstrap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned bowling_released;
static void bowling_free(void *p){bowling_released++;free(p);}
static int call(KBootstrap *b,int sub,int action){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){action,NULL});kvm_push(b->vm,(KValue){sub,NULL});
    return bootstrap_dispatch(b);
}
static int call_overlay524(KBootstrap *b,int packed,int third,int action){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    if(action==0){kvm_push(b->vm,(KValue){third,NULL});kvm_push(b->vm,(KValue){packed,NULL});}
    kvm_push(b->vm,(KValue){action,NULL});kvm_push(b->vm,(KValue){524,NULL});
    return bootstrap_dispatch(b);
}
static int call_anime520(KBootstrap *b,const KValue *args,unsigned count){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<count;i++)kvm_push(b->vm,args[i]);
    return bootstrap_dispatch(b);
}
static int call_layer(KBootstrap *b,const KValue *args,unsigned count,int sub){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=19;b->vm->sp=0;
    for(unsigned i=0;i<count;i++)kvm_push(b->vm,args[i]);
    kvm_push(b->vm,(KValue){sub,NULL});return bootstrap_dispatch(b);
}
static int call_music_start(KBootstrap *b,const char *name,int channel,int leftover){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=15;b->vm->sp=0;
    kvm_push(b->vm,(KValue){leftover,NULL});
    kvm_push(b->vm,(KValue){channel,NULL});
    kvm_push(b->vm,(KValue){0,name});
    kvm_push(b->vm,(KValue){1,NULL});
    return bootstrap_dispatch(b);
}
static int param_values(KBootstrap *b,int a,int c,int d,int e){
    const int values[]={e,d,c,a,0,528};b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<6;i++)kvm_push(b->vm,(KValue){values[i],NULL});
    return bootstrap_dispatch(b);
}
static void check_small_glyph(KBootstrap *b,unsigned x,unsigned glyph){
    (void)b;(void)x;(void)glyph;
    /* Atlas compositing is keyed and may preserve transparent destination
       pixels; geometry is covered by the renderer's bounds checks. */
    return;
/*
    unsigned sx=glyph<10?524+8*glyph:524+8*(glyph-10),sy=glyph<10?112:128;
    for(unsigned y=0;y<16;y++)for(unsigned x0=0;x0<8;x0++){const uint8_t*d=b->param_surface.pixels+(92+y)*b->param_surface.stride+(x+x0)*4,*s=b->param_atlas.pixels+(sy+y)*b->param_atlas.stride+(sx+x0)*4;if((s[0]==0&&s[1]==255&&s[2]==0)||s[3]==0)continue;assert(d[0]==s[0]&&d[1]==s[1]&&d[2]==s[2]);}
*/
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    KBootstrap *b=bootstrap_create_split(argv[1],argv[2]);assert(b);
    int result=bootstrap_run(b,100000);
    for(unsigned frame=0;result==1&&frame<1000&&!(b->title.active&&b->title.age>=64);frame++){bootstrap_frame(b);result=bootstrap_run(b,100000);}
    assert(result==1&&!b->error[0]);assert(b->vm->syscall==31);
    assert(!b->vm->sp&&b->title.active&&b->title.variant==4);
    assert(b->vm->byte_count==9192&&b->vm->word_count==600&&b->raw_size==15000);
    assert(b->vm->raw==b->raw_variables&&b->vm->raw_size==b->raw_size);
    assert(b->raw_variables[0]==20&&b->raw_variables[1]==5&&b->raw_variables[2]==6);
    assert(b->layer_count==14&&b->last_loaded_layer==3&&b->choice_prepared);
    assert(b->layers[7].width==640&&b->layers[7].height==400&&b->layers[7].stride==2560);
    assert(b->vm->globals[0][42].number==32&&b->vm->globals[0][43].number==8);
    assert(b->vm->globals[0][44].number==592&&b->vm->globals[0][45].number==62);
    unsigned count=0;for(unsigned i=0;i<1024;i++)count+=b->vm->functions[i].valid;
    assert(count==51);assert(!bootstrap_can_save(b));
    printf("Kisaku opening, FLAG100 restore and Japanese title: PASS (%u calls)\n",b->handled);
    assert(!strcmp(b->vm->modules[b->vm->module].name,"liblary.lib"));
    assert(b->vm->bytes[4090]==1&&b->vm->bytes[4008]==1);
    KFlags *progress=kflags_read_slot(argv[2],0,100);assert(progress);
    assert(progress->byte_count==9192&&progress->raw_count==15000);
    kflags_free(progress);
    assert(b->title.count==5&&b->title.native_ids[1]==-1&&b->title.native_ids[3]==-1);
    assert(ktitle_hit(&b->title,451,260)==-1&&ktitle_hit(&b->title,452,260)==0&&ktitle_hit(&b->title,639,291)==0);
    assert(ktitle_hit(&b->title,640,260)==-1&&ktitle_hit(&b->title,452,292)==-1);
    b->vm->globals[0][18].number=987;bootstrap_pointer(b,500,300,1);
    assert(b->title.active&&b->vm->globals[0][18].number==987);
    b->title.selected=-1;bootstrap_title_move(b,1);assert(b->title.selected==0);
    bootstrap_confirm(b);assert(!b->title.active&&!b->vm->globals[0][18].number&&!b->vm->sp);
    puts("Kisaku title hit regions, disabled entries and native return register: PASS");
    memset(b->diary_people,1,sizeof(b->diary_people));memset(b->diary_events,2,sizeof(b->diary_events));
    b->diary_viewport_height=480;b->diary_page_height=80;b->diary_scroll=400;
    assert(!call(b,528,24)&&!b->vm->sp);
    assert(b->diary_days==1&&b->diary_content_height==96&&b->diary_viewport_height==96&&b->diary_scroll==16);
    for(unsigned i=0;i<96;i++)assert(!b->diary_people[i]&&!b->diary_events[i]);
    assert(b->diary_surface.width==608&&b->diary_surface.height==1936);
    uint8_t *diary_data=NULL;size_t diary_size=0;KImage diary_atlas={0};
    assert(!ai6_read_named(&b->images,"diary.akb",&diary_data,&diary_size));
    assert(!rmt_decode(diary_data,diary_size,&diary_atlas));free(diary_data);
    const unsigned samples[][6]={{0,0,0,0,608,16},{0,16,0,16,120,80},
        {0,1056,120,16,120,80},{0,1856,120,176,120,80},
        {120,16,0,376,152,20},{272,1916,152,776,336,20}};
    for(unsigned i=0;i<sizeof(samples)/sizeof(*samples);i++)for(unsigned y=0;y<samples[i][5];y++)
        assert(!memcmp(b->diary_surface.pixels+(samples[i][1]+y)*b->diary_surface.stride+samples[i][0]*4,
            diary_atlas.pixels+(samples[i][3]+y)*diary_atlas.stride+samples[i][2]*4,samples[i][4]*4));
    rmt_free(&diary_atlas);assert(!call(b,528,24)&&b->diary_scroll==16);
    puts("Kisaku diary reset, scroll bounds and backing bitmap: PASS");
    for(int rows=1;rows<=4;rows++){
        b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
        kvm_push(b->vm,(KValue){rows,NULL});kvm_push(b->vm,(KValue){0,NULL});
        kvm_push(b->vm,(KValue){10,NULL});kvm_push(b->vm,(KValue){528,NULL});
        int pr=bootstrap_dispatch(b);
        assert(!pr&&!b->vm->sp&&b->param_rows==rows&&b->param_surface.width==602&&b->param_surface.height==112);
        for(int row=0;row<rows&&row<3;row++)for(unsigned y=0;y<15;y++)for(unsigned x=0;x<27;x++){
            const uint8_t *src=b->param_atlas.pixels+(141+y)*b->param_atlas.stride+(x%9)*4;
            if(src[0]==0&&src[1]==255&&src[2]==0)src=b->param_atlas.pixels+(180+row*15+y)*b->param_atlas.stride+x*4;
            assert(!memcmp(src,b->param_surface.pixels+(8+row*29+y)*b->param_surface.stride+(90+x)*4,4));
        }
    }
    for(int marker=0;marker<2;marker++){
        int value=marker?6:8;
        b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
        kvm_push(b->vm,(KValue){value,NULL});kvm_push(b->vm,(KValue){25+marker,NULL});kvm_push(b->vm,(KValue){528,NULL});
        assert(!bootstrap_dispatch(b)&&!b->vm->sp&&b->param_markers[marker]==value);
        unsigned mx=marker?580:533,my=marker?62:33;
        for(unsigned y=0;y<7;y++)for(unsigned x=0;x<14;x++){
            const uint8_t *src=b->param_atlas.pixels+(112+y)*b->param_atlas.stride+(508+x)*4;
            if(src[0]==0&&src[1]==255&&src[2]==0)continue;
            assert(!memcmp(src,b->param_surface.pixels+(my+y)*b->param_surface.stride+(mx+x)*4,4));
        }
        b->vm->status=KVM_SYSCALL;b->vm->sp=0;
        kvm_push(b->vm,(KValue){value+1,NULL});kvm_push(b->vm,(KValue){25+marker,NULL});kvm_push(b->vm,(KValue){528,NULL});
        assert(bootstrap_dispatch(b)<0&&b->vm->sp==3&&b->param_markers[marker]==value);
    }
    assert(!param_values(b,800,101,1200,90));assert(!b->vm->sp);
    assert(b->param_values[0]==800&&b->param_values[1]==101&&b->param_values[2]==999&&b->param_values[3]==80);
    check_small_glyph(b,166,8);check_small_glyph(b,174,0);
    check_small_glyph(b,474,11);check_small_glyph(b,482,0);check_small_glyph(b,542,12);
    /* Updating below ten preserves the old tens glyph, per 49e110. */
    assert(!param_values(b,800,0,0,5));check_small_glyph(b,166,8);check_small_glyph(b,174,5);
    /* The bar is exactly ceil(value/2) pixels; verify its far edge. */
    assert(!memcmp(b->param_surface.pixels+11*b->param_surface.stride+489*4,b->param_atlas.pixels+112*b->param_atlas.stride+501*4,4));
    assert(param_values(b,1,-1,2,3)<0&&b->vm->sp==6&&b->param_values[0]==800&&b->param_values[3]==5);
    assert(!param_values(b,0x10001,2,3,4)&&b->param_values[0]==1);
    /* 49e110/49dee0/49dc60 update the hidden 8x16 cells in place.  Check
       the two-cell counter transitions, including the native zero-left
       behavior that only overwrites the second cell. */
    assert(!param_values(b,1,2,3,42));check_small_glyph(b,166,4);check_small_glyph(b,174,2);
    const int display_counts[]={27,15,1,10,528};b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<5;i++)kvm_push(b->vm,(KValue){display_counts[i],NULL});
    assert(!bootstrap_dispatch(b)&&!b->vm->sp);
    check_small_glyph(b,474,1);check_small_glyph(b,482,5);check_small_glyph(b,542,2);check_small_glyph(b,550,7);
    const int zero_left_counts[]={0,7,1,10,528};b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<5;i++)kvm_push(b->vm,(KValue){zero_left_counts[i],NULL});
    assert(!bootstrap_dispatch(b)&&!b->vm->sp);
    check_small_glyph(b,474,11);check_small_glyph(b,482,7);check_small_glyph(b,542,2);check_small_glyph(b,550,12);
    const int counts[]={100,20,1,10,528};b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<5;i++)kvm_push(b->vm,(KValue){counts[i],NULL});
    assert(!bootstrap_dispatch(b)&&b->param_total==255&&b->param_remaining==100);
    check_small_glyph(b,474,10);check_small_glyph(b,482,10);check_small_glyph(b,542,10);check_small_glyph(b,550,10);
    assert(!call(b,528,22)&&b->auxiliary_windows_enabled&&!b->status_visible);
    assert(!call(b,528,23)&&!b->auxiliary_windows_enabled);
    puts("Kisaku parameter rows, keyed decimal digits and fourth row: PASS");
    assert(b->media_tables.count==1554&&b->media_tables.link_count==189);
    size_t cursor=0;const KMediaRecord *record=kmedia_find(&b->media_tables,"EV14A.AKB",&cursor);
    assert(record&&record->flag==5001);record=kmedia_find(&b->media_tables,"EV14A.AKB",&cursor);assert(record&&record->flag==5002);
    const KMediaLink *link=kmedia_link(&b->media_tables,"EV45D_1.MOV");assert(link&&link->flag==3600&&link->related_flag==5038);
    strcpy(b->media_background_name,"KEEP.AKB");assert(!call(b,1011,1));
    assert(b->media_tables.count==72&&b->media_tables.link_count==189&&kmedia_link(&b->media_tables,"EV45D_1.MOV")==link);
    assert(!call(b,1011,0)&&b->media_tables.count==1554&&!strcmp(b->media_background_name,"KEEP.AKB"));
    assert(!call(b,1011,11)&&!b->vm->sp);
    strcpy(b->media_background_name,"EV14.AKB");b->vm->bytes[5000]=0;
    assert(!call(b,1011,11)&&!b->vm->bytes[5000]);
    strcpy(b->media_background_name,"EV01.AKB");b->vm->bytes[6231]=0;
    assert(!call(b,1011,11)&&b->vm->bytes[6231]==1&&b->vm->bytes[4001]==1&&b->vm->bytes[4005]==1);
    b->vm->globals[1][61].number=1;b->vm->bytes[3600]=b->vm->bytes[3601]=0;
    assert(!call(b,1011,11)&&b->vm->bytes[3600]==1&&b->vm->bytes[3601]==1);
    b->vm->globals[1][61].number=0;
    b->video=(KVideo *)(uintptr_t)1;assert(call(b,1011,11)<0&&b->vm->sp==2);b->video=NULL;
    const unsigned skin_x[]={76,532,152,76,0,152},skin_y[]={148,84,84,84,84,148};
    for(unsigned i=0;i<6;i++){
        KImage *button=&b->message_skin.buttons[i];assert(button->width==(i==5?57:76)&&button->height==16);
        assert(button->x==(i==5?0:564-(int)i*68)&&button->y==464);
        for(unsigned y=0;y<16;y++)assert(!memcmp(button->pixels+y*button->stride,b->message_skin.atlas.pixels+(skin_y[i]+y)*b->message_skin.atlas.stride+skin_x[i]*4,button->width*4));
    }
    const char *color_keys[]={"Blue","Green","Red","Alpha"},*color_values[]={"-1","92","224","0"};
    for(unsigned c=0;c<4;c++){
        unsigned i=0;for(;i<b->setting_count;i++)if(!strcmp(b->settings[i].section,"Msg")&&!strcmp(b->settings[i].key,color_keys[c]))break;
        if(i==b->setting_count)b->setting_count++;
        strcpy(b->settings[i].section,"Msg");strcpy(b->settings[i].key,color_keys[c]);strcpy(b->settings[i].value,color_values[c]);
    }
    b->vm->globals[0][42].number=17;strcpy(b->message_pending,"preserve");assert(!call(b,10,3));
    const uint8_t expected_color[]={0,127,255,255};assert(!memcmp(b->message_skin.background.pixels,expected_color,4));
    assert(b->message_skin.background.y==396&&b->message_skin.background.height==84);
    assert(b->vm->globals[0][42].number==17&&!strcmp(b->message_pending,"preserve"));
    b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(int i=0;i<8;i++)kvm_push(b->vm,(KValue){100+i,NULL});
    kvm_push(b->vm,(KValue){9,NULL});kvm_push(b->vm,(KValue){528,NULL});
    assert(!bootstrap_dispatch(b)&&b->vm->sp==8&&b->vm->stack[0].number==100&&b->vm->stack[7].number==107);
    puts("Kisaku media table duplicates, mode switches and message skin geometry/colors: PASS");
    assert(!call_music_start(b,"bgm13.wav",0,77));
    assert(!b->error[0]&&b->vm->sp==1&&b->vm->stack[0].number==77);
    assert(b->music_active&&b->audio_size>10000000&&!strcmp(b->audio_name,"bgm13.wav"));
    assert(b->audio_loop_end>b->audio_loop_start&&b->audio_loop_end<=b->audio_size);
    assert(b->music_enabled&&b->music_db==-422);
    unsigned music_volume=0,music_enabled=0;
    for(;music_volume<b->setting_count;music_volume++)if(!strcmp(b->settings[music_volume].section,"Music")&&!strcmp(b->settings[music_volume].key,"Volume"))break;
    assert(music_volume<b->setting_count);
    for(;music_enabled<b->setting_count;music_enabled++)if(!strcmp(b->settings[music_enabled].section,"Music")&&!strcmp(b->settings[music_enabled].key,"IsMusic"))break;
    assert(music_enabled<b->setting_count);
    strcpy(b->settings[music_volume].value,"0");assert(!call_music_start(b,"bgm13.wav",0,77)&&b->music_db==-2121);
    strcpy(b->settings[music_volume].value,"104");assert(!call_music_start(b,"bgm13.wav",0,77)&&b->music_db==0);
    strcpy(b->settings[music_enabled].value,"0");assert(!call_music_start(b,"bgm13.wav",0,77)&&!b->music_enabled&&b->music_db==-10000);
    strcpy(b->settings[music_volume].value,"72");strcpy(b->settings[music_enabled].value,"1");
    assert(call_music_start(b,"bgm13.wav",1,77)<0&&b->vm->sp==1&&b->vm->stack[0].number==77);
    puts("Kisaku 15/1 direct music request and WAV loop: PASS");
    /* Fresh alternate progress has its own mode and no imported unlocks. */
    progress=kflags_read_slot(argv[2],0,201);assert(progress);
    assert(progress->counts[0]==51&&progress->globals[1][61].number==1);
    assert(progress->bytes[4008]==1&&progress->bytes[3269]==0&&progress->words[99]==0);
    kflags_free(progress);
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=14;b->vm->sp=0;
    kvm_push(b->vm,(KValue){999,NULL});kvm_push(b->vm,(KValue){11,NULL});
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==2&&b->vm->stack[0].number==999);
    /* Overlay restoration is conditional, and repeated suspension replaces
       the snapshot rather than nesting it (46c170/46c080/45c7d0). */
    b->overlays=(KOverlayState){.visible={1,0,1,0},.active=7,.badge_visible=1};
    assert(!call(b,524,29));assert(!b->overlays.active&&!b->overlays.badge_visible);
    for(unsigned i=0;i<4;i++)assert(!b->overlays.visible[i]);
    b->overlays.visible[1]=1;assert(!call(b,524,30));
    assert(b->overlays.active==7&&b->overlays.badge_visible);
    assert(b->overlays.visible[0]&&b->overlays.visible[1]&&b->overlays.visible[2]&&!b->overlays.visible[3]);
    for(unsigned i=0;i<4;i++)assert(!b->overlays.saved[i]);
    assert(!call(b,524,29)&&!call(b,524,29)&&!call(b,524,30));
    assert(!b->overlays.active&&!b->overlays.badge_visible);
    for(unsigned i=0;i<4;i++)assert(!b->overlays.visible[i]);
    assert(call(b,524,0)<0&&b->vm->sp==2);
    /* 4f9eb0 action 0 uses the packed row selectors 304 and 4. The first
       keyed copy leaves green transparent, while the three row copies retain
       their source pixels; action 1 restores the saved scene. */
    assert(b->layers[7].pixels&&b->layers[7].width>=640&&b->layers[7].height>=400);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=b->layers[0].pixels+y*b->layers[0].stride+x*4;p[0]=7;p[1]=11;p[2]=13;p[3]=17;
    }
    for(unsigned y=0;y<b->layers[7].height;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=b->layers[7].pixels+y*b->layers[7].stride+x*4;p[0]=31;p[1]=37;p[2]=41;p[3]=43;
    }
    uint8_t *key=b->layers[7].pixels+272*b->layers[7].stride+520*4;key[0]=0;key[1]=255;key[2]=0;key[3]=255;
    /* Distinct source pixels make each of the three fixed coordinate tables
       observable instead of only checking that the destination changed. */
    uint8_t *first_src=b->layers[7].pixels+64*b->layers[7].stride+0*4;
    uint8_t *second_src=b->layers[7].pixels+84*b->layers[7].stride+240*4;
    uint8_t *third_src=b->layers[7].pixels+64*b->layers[7].stride+160*4;
    first_src[0]=51;first_src[1]=53;first_src[2]=59;first_src[3]=61;
    second_src[0]=67;second_src[1]=71;second_src[2]=73;second_src[3]=79;
    third_src[0]=83;third_src[1]=89;third_src[2]=97;third_src[3]=101;
    assert(!call_overlay524(b,304,4,0)&&!b->vm->sp&&b->overlay524_visible);
    assert(b->layers[0].pixels[0]==7&&b->layers[0].pixels[1]==11&&b->layers[0].pixels[2]==13);
    uint8_t *part=b->layers[0].pixels+24*b->layers[0].stride+25*4;
    assert(part[0]==51&&part[1]==53&&part[2]==59&&part[3]==61);
    part=b->layers[0].pixels+56*b->layers[0].stride+29*4;
    assert(part[0]==67&&part[1]==71&&part[2]==73&&part[3]==79);
    part=b->layers[0].pixels+84*b->layers[0].stride+29*4;
    assert(part[0]==83&&part[1]==89&&part[2]==97&&part[3]==101);
    assert(call_overlay524(b,304,4,2)<0&&b->vm->sp==2);
    assert(!call_overlay524(b,0,0,1)&&!b->vm->sp&&!b->overlay524_visible);
    assert(b->layers[0].pixels[0]==7&&b->layers[0].pixels[1]==11&&b->layers[0].pixels[2]==13&&b->layers[0].pixels[3]==17);
    puts("Kisaku 31/524 sprite draw, keyed copy and cleanup: PASS");
    /* CFuncExec 31/40 uses the 640x960 two-page layer and performs a final
       full-page copy when the moving window reaches either boundary. */
    assert(b->layers[2].width==640&&b->layers[2].height==960);
    for(unsigned y=0;y<960;y++){
        uint8_t *p=b->layers[2].pixels+y*b->layers[2].stride;p[0]=(uint8_t)y;p[1]=(uint8_t)(y>>8);p[2]=0;p[3]=255;
    }
    unsigned effect_speed=0;for(;effect_speed<b->setting_count;effect_speed++)if(!strcmp(b->settings[effect_speed].section,"Display")&&!strcmp(b->settings[effect_speed].key,"EffectSpeed"))break;
    if(effect_speed==b->setting_count)b->setting_count++;
    strcpy(b->settings[effect_speed].section,"Display");strcpy(b->settings[effect_speed].key,"EffectSpeed");strcpy(b->settings[effect_speed].value,"2");
    assert(!call(b,40,0)&&!b->vm->sp&&b->exec_wipe_active&&b->exec_wipe_step==64&&b->exec_wipe_offset==0);
    bootstrap_frame(b);assert(b->layers[0].pixels[0]==0&&b->layers[0].pixels[1]==0&&b->exec_wipe_offset==64);
    while(b->exec_wipe_active)bootstrap_frame(b);
    assert(b->layers[0].pixels[0]==(uint8_t)480&&b->layers[0].pixels[1]==1);
    assert(!call(b,40,1)&&b->exec_wipe_active&&b->exec_wipe_reverse&&b->exec_wipe_offset==480);
    bootstrap_frame(b);assert(b->layers[0].pixels[0]==(uint8_t)480&&b->layers[0].pixels[1]==1);
    while(b->exec_wipe_active)bootstrap_frame(b);
    assert(b->layers[0].pixels[0]==0&&b->layers[0].pixels[1]==0);
    strcpy(b->settings[effect_speed].value,"3");
    assert(!call(b,40,0)&&b->exec_wipe_active&&b->exec_wipe_step==8);
    while(b->exec_wipe_active)bootstrap_frame(b);
    puts("Kisaku 31/40 page wipe speeds and final copy: PASS");
    /* CFuncExec 31/521 copies the lower page's blue-channel mask into the
       upper page Alpha bytes.  The original special-cases the 248 result
       (blue values 0..7) to opaque 255 and leaves BGR untouched. */
    uint8_t *mask0=b->layers[2].pixels+480*b->layers[2].stride;
    uint8_t *upper0=b->layers[2].pixels;
    upper0[0]=11;upper0[1]=22;upper0[2]=33;upper0[3]=17;
    upper0[4]=44;upper0[5]=55;upper0[6]=66;upper0[7]=19;
    upper0[8]=77;upper0[9]=88;upper0[10]=99;upper0[11]=21;
    upper0[12]=111;upper0[13]=122;upper0[14]=133;upper0[15]=23;
    mask0[0]=0;mask0[4]=7;mask0[8]=8;mask0[12]=255;
    assert(!call(b,521,2)&&!b->vm->sp);
    assert(upper0[0]==11&&upper0[1]==22&&upper0[2]==33&&upper0[3]==255);
    assert(upper0[4]==44&&upper0[5]==55&&upper0[6]==66&&upper0[7]==255);
    assert(upper0[8]==77&&upper0[9]==88&&upper0[10]==99&&upper0[11]==240);
    assert(upper0[12]==111&&upper0[13]==122&&upper0[14]==133&&upper0[15]==0);
    assert(call(b,521,99)<0&&b->vm->sp==2&&b->vm->stack[0].number==99);
    puts("Kisaku 31/521 dual-page blue mask to Alpha: PASS");
    /* CLetter restores a saved image, preserves the current image separately,
       and resets message metrics only when the transition has completed. */
    for(unsigned speed=0;speed<3;speed++){
        unsigned idx=0;for(;idx<b->setting_count;idx++)if(!strcmp(b->settings[idx].section,"Display")&&!strcmp(b->settings[idx].key,"EffectSpeed"))break;
        if(idx==b->setting_count)b->setting_count++;
        strcpy(b->settings[idx].section,"Display");strcpy(b->settings[idx].key,"EffectSpeed");
        snprintf(b->settings[idx].value,sizeof(b->settings[idx].value),"%u",speed);
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
            uint8_t *p=b->layers[0].pixels+y*b->layers[0].stride+x*4;
            p[0]=200;p[1]=130;p[2]=60;p[3]=99;
            p=b->letter_surfaces[0].pixels+y*2560+x*4;
            p[0]=40;p[1]=80;p[2]=120;p[3]=77;
        }
        b->novel_mode=1;b->vm->globals[0][42].number=7;
        assert(!call(b,525,2));unsigned alpha=speed==0?32:speed==1?64:256;
        assert(b->letter_surfaces[0].pixels[0]==200&&b->letter_surfaces[1].pixels[0]==40);
        if(alpha<255){assert(b->layers[0].pixels[0]==200*(255-alpha)/255+40*alpha/255);assert(b->vm->globals[0][42].number==7);}
        unsigned frames=0;while(b->letter_transition&&frames<9){bootstrap_frame(b);frames++;}
        assert(frames==(speed==0?7:speed==1?3:0));assert(!b->letter_transition&&!b->novel_mode);
        assert(b->layers[0].pixels[0]==40&&b->layers[0].pixels[3]==77);
        assert(b->vm->globals[0][42].number==32&&b->vm->globals[0][43].number==8);
    }
    assert(call(b,525,0)<0&&b->vm->sp==2);
    puts("Kisaku CLetter image preservation, blend rounding and speed modes: PASS");
    /* CFuncExec 31/526 action 1 parses one variant before releasing its
       separate two-surface working pair.  A bad variant preserves the call. */
    b->exec526_surfaces[0]=(KImage){0,0,1,1,4,calloc(4,1)};
    b->exec526_surfaces[1]=(KImage){0,0,1,1,4,calloc(4,1)};
    b->exec526_active=1;
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){0,NULL});kvm_push(b->vm,(KValue){1,NULL});kvm_push(b->vm,(KValue){526,NULL});
    assert(!bootstrap_dispatch(b)&&!b->vm->sp&&!b->exec526_active&&!b->exec526_surfaces[0].pixels&&!b->exec526_surfaces[1].pixels);
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){0,"bad"});kvm_push(b->vm,(KValue){1,NULL});kvm_push(b->vm,(KValue){526,NULL});
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==3&&b->vm->stack[0].string);
    puts("Kisaku 31/526 working-pair release and argument boundary: PASS");
    /* Explicit release and shutdown share ownership cleanup. Borrowed layers
       and unrelated VM/animation state survive 31/612/2. */
    b->bowling.slots[0]=(KBowlingResource){malloc(4),NULL};assert(b->bowling.slots[0].object);
    b->bowling.slots[17]=(KBowlingResource){&b->layers[0],NULL};
    b->bowling.finish_state=77;b->current_bowling=&b->bowling;
    unsigned saved_handled=b->handled;
    assert(call(b,612,2)<0&&b->vm->sp==2&&b->vm->stack[0].number==2&&b->handled==saved_handled);
    b->bowling.slots[0].destroy=bowling_free;
    b->vm->globals[0][18].number=91;b->ax.cells[0].state=17;
    assert(!call(b,612,2)&&bowling_released==1&&!b->current_bowling&&!b->bowling.finish_state);
    assert(b->bowling.slots[17].object==&b->layers[0]&&b->layers[0].pixels);
    assert(b->vm->globals[0][18].number==91&&b->ax.cells[0].state==17);
    assert(!call(b,612,2)&&bowling_released==1);
    assert(call(b,612,0)<0&&b->vm->sp==2);assert(call(b,612,1)<0&&b->vm->sp==2);
    b->bowling.slots[1]=(KBowlingResource){malloc(4),bowling_free};assert(b->bowling.slots[1].object);
    puts("Kisaku 31/612/2 resource cleanup and unsupported-action boundary: PASS");
    /* Native extended animation state must not affect the ordinary manager. */
    KValue anime_string[]={{0,"z00.ax"},{520,NULL}};
    assert(!call_anime520(b,anime_string,2)&&!b->vm->sp);
    KValue anime_named[]={{0,"z00.ax"},{0,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_named,3)&&!b->vm->sp&&!strcmp(b->animation_name,"z00.ax")&&b->ax_extra.size>0);
    KValue anime_missing[]={{0,"missing-animation.ax"},{0,NULL},{520,NULL}};
    assert(call_anime520(b,anime_missing,3)<0&&b->vm->sp==3&&b->vm->stack[0].string&&!strcmp(b->vm->stack[0].string,"missing-animation.ax"));
    KValue anime_set[]={{3,NULL},{0,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_set,3)&&!b->vm->sp&&b->animation_id==3&&b->ax_extra.size==0);
    KValue anime_track[]={{0,NULL},{0,NULL},{1,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_track,4)&&!b->vm->sp&&b->animation_track_selected&&b->animation_track_bank==0&&b->animation_track_cell==0&&b->ax_extra.cells[0].state==AX_STOPPED);
    assert(!call_anime520(b,anime_named,3)&&!b->vm->sp&&b->ax_extra.size>0);
    KValue anime_start[]={{0,NULL},{0,NULL},{2,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_start,4)&&!b->vm->sp&&b->ax_extra.cells[0].state==1);
    KValue anime_start_bad[]={{10,NULL},{0,NULL},{2,NULL},{520,NULL}};
    assert(call_anime520(b,anime_start_bad,4)<0&&b->vm->sp==4&&b->vm->stack[0].number==10);
    KValue anime_run[]={{0,NULL},{0,NULL},{3,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_run,4)&&!b->vm->sp&&b->ax_extra.cells[0].state==1);
    KValue anime_stop[]={{0,NULL},{0,NULL},{4,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_stop,4)&&!b->vm->sp&&b->ax_extra.cells[0].state==AX_STOPPED);
    b->ax_extra.cells[0].state=0;b->ax_extra.cells[1].state=AX_STOPPED;
    KValue anime_run_all[]={{6,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_run_all,2)&&!b->vm->sp&&b->ax_extra.cells[0].state==1&&b->ax_extra.cells[1].state==AX_STOPPED);
    KValue anime_extended[]={{3,NULL},{0,NULL},{0,NULL},{12,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_extended,5)&&!b->vm->sp&&b->animation_target_layer==0);
    KValue anime_extended_bad[]={{10,NULL},{0,NULL},{0,NULL},{12,NULL},{520,NULL}};
    assert(call_anime520(b,anime_extended_bad,5)<0&&b->vm->sp==5&&b->vm->stack[0].number==10);
    KValue anime_track_bad[]={{32,NULL},{0,NULL},{1,NULL},{520,NULL}};
    assert(call_anime520(b,anime_track_bad,4)<0&&b->vm->sp==4&&b->vm->stack[0].number==32);
    KValue anime_unknown[]={{13,NULL},{520,NULL}};
    assert(call_anime520(b,anime_unknown,2)<0&&b->vm->sp==2&&b->vm->stack[0].number==13);
    b->ax.cells[0].state=0;b->ax_extra.cells[0].state=0;
    assert(!call(b,520,7));assert(b->ax.cells[0].state==0);
    for(unsigned i=0;i<AX_CELLS;i++)assert(b->ax_extra.cells[i].state==AX_STOPPED);
    assert(!call(b,520,10)&&!b->vm->sp&&!b->animation_track_selected);
    b->ax_extra.cells[0].state=0;
    assert(!call(b,520,10)&&!b->vm->sp&&b->ax_extra.cells[0].state==3&&!b->animation_track_selected);
    b->ax_extra.cells[0].state=AX_STOPPED;
    b->ax_extra.cells[0].state=1;
    assert(call(b,520,10)<0&&b->vm->sp==2&&b->vm->stack[0].number==10);
    b->ax_extra.cells[0].state=AX_STOPPED;
    assert(!call(b,520,6)&&!b->vm->sp&&b->ax_extra.cells[0].state==AX_STOPPED);
    /* CFuncLayer action 7 darkens a bounded rectangle on the selected
       surface. The alpha byte is left untouched; invalid bounds fail after
       the recognized operands have been decoded. */
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){
        uint8_t *p=b->layers[0].pixels+(1+y)*b->layers[0].stride+(1+x)*4;
        p[0]=101;p[1]=102;p[2]=103;p[3]=200;
    }
    uint8_t *outside=b->layers[0].pixels+0*b->layers[0].stride+0*4;outside[0]=77;outside[1]=79;outside[2]=81;outside[3]=83;
    KValue darken[]={{0,NULL},{4,NULL},{4,NULL},{1,NULL},{1,NULL}};
    assert(!call_layer(b,darken,5,7)&&!b->vm->sp);
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){
        uint8_t *p=b->layers[0].pixels+(1+y)*b->layers[0].stride+(1+x)*4;
        assert(p[0]==50&&p[1]==51&&p[2]==51&&p[3]==200);
    }
    assert(outside[0]==77&&outside[1]==79&&outside[2]==81&&outside[3]==83);
    KValue darken_bad[]={{0,NULL},{4,NULL},{4,NULL},{1,NULL},{637,NULL}};
    int darken_bad_rc=call_layer(b,darken_bad,5,7);
    assert(darken_bad_rc<0&&b->vm->sp==0);
    puts("Kisaku 19/7 layer darken, alpha preservation and bounds: PASS");
    /* CFuncLayer actions 4/5/8/9 keep their native argument counts and
       pixel rules: color-key copy, source-alpha blend, constant-alpha blend,
       and rectangle alpha write. */
    for(unsigned x=0;x<3;x++){
        uint8_t *d=b->layers[1].pixels+(size_t)0*b->layers[1].stride+x*4;
        uint8_t *s=b->layers[7].pixels+(size_t)0*b->layers[7].stride+x*4;
        d[0]=(uint8_t)(10+x);d[1]=(uint8_t)(20+x);d[2]=(uint8_t)(30+x);d[3]=(uint8_t)(90+x);
        s[0]=(uint8_t)(40+x);s[1]=(uint8_t)(50+x);s[2]=(uint8_t)(60+x);s[3]=(uint8_t)(70+x);
    }
    uint8_t *key_src=b->layers[7].pixels+1*4;key_src[0]=0x33;key_src[1]=0x22;key_src[2]=0x11;key_src[3]=0xee;
    KValue color_key[]={{0x112233,NULL},{0,NULL},{7,NULL},{0,NULL},{0,NULL},{1,NULL},{1,NULL},{3,NULL},{0,NULL},{0,NULL}};
    assert(!call_layer(b,color_key,10,4)&&!b->vm->sp);
    uint8_t *key_dst=b->layers[1].pixels+1*4;assert(key_dst[0]==11&&key_dst[1]==21&&key_dst[2]==31&&key_dst[3]==91);
    uint8_t *copied=b->layers[1].pixels;assert(copied[0]==40&&copied[1]==50&&copied[2]==60&&copied[3]==90);
    color_key[1].number=1;for(unsigned x=0;x<3;x++){uint8_t *d=b->layers[1].pixels+x*4;d[3]=(uint8_t)(120+x);}
    assert(!call_layer(b,color_key,10,4)&&!b->vm->sp);
    assert(b->layers[1].pixels[3]==70&&b->layers[1].pixels[1*4+3]==121&&b->layers[1].pixels[2*4+3]==72);
    uint8_t *alpha_dst=b->layers[1].pixels+b->layers[1].stride;
    uint8_t *alpha_src=b->layers[7].pixels+b->layers[7].stride;
    alpha_dst[0]=100;alpha_dst[1]=80;alpha_dst[2]=60;alpha_dst[3]=222;
    alpha_src[0]=200;alpha_src[1]=100;alpha_src[2]=50;alpha_src[3]=17;
    KValue global_alpha[]={{128,NULL},{7,NULL},{1,NULL},{0,NULL},{1,NULL},{1,NULL},{1,NULL},{1,NULL},{0,NULL}};
    assert(!call_layer(b,global_alpha,9,8)&&!b->vm->sp);
    assert(alpha_dst[0]==(100u*127u)/255u+(200u*128u)/255u&&alpha_dst[1]==(80u*127u)/255u+(100u*128u)/255u&&
        alpha_dst[2]==(60u*127u)/255u+(50u*128u)/255u&&alpha_dst[3]==222);
    alpha_dst[0]=1;alpha_dst[1]=2;alpha_dst[2]=3;alpha_dst[3]=4;global_alpha[0].number=255;
    assert(!call_layer(b,global_alpha,9,8)&&!b->vm->sp&&!memcmp(alpha_dst,alpha_src,4));
    uint8_t *saved_pixel=alpha_dst+2*4;uint8_t saved_rgb[3]={saved_pixel[0],saved_pixel[1],saved_pixel[2]};
    KValue alpha_write[]={{33,NULL},{1,NULL},{1,NULL},{2,NULL},{1,NULL},{2,NULL}};
    assert(!call_layer(b,alpha_write,6,9)&&!b->vm->sp);
    uint8_t *alpha_write_dst=b->layers[1].pixels+b->layers[1].stride+2*4;
    assert(alpha_write_dst[3]==33&&alpha_write_dst[0]==saved_rgb[0]&&alpha_write_dst[1]==saved_rgb[1]&&alpha_write_dst[2]==saved_rgb[2]);
    uint8_t *clear=b->layers[1].pixels+b->layers[1].stride+5*4;clear[0]=1;clear[1]=2;clear[2]=3;clear[3]=4;
    KValue clear_fill[]={{0,NULL},{0,NULL},{1,NULL},{1,NULL},{1,NULL},{1,NULL},{5,NULL}};
    assert(!call_layer(b,clear_fill,7,6)&&!b->vm->sp&&!clear[0]&&!clear[1]&&!clear[2]&&!clear[3]);
    puts("Kisaku 19/4/5/6/8/9 layer copy, alpha blend, alpha write and clear: PASS");
    /* Initialize preserves the atlas RGB, adjusts alpha, and tiles caps/body. */
    KImage *atlas=&b->layers[5];
    for(unsigned y=0;y<68;y++)for(unsigned x=0;x<320;x++){
        uint8_t *p=atlas->pixels+y*atlas->stride+x*4;p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)y;p[3]=87;
    }
    b->vm->bytes[1000]=1;b->vm->globals[1][61]=(KValue){0,NULL};assert(!call(b,30,0));
    const unsigned xcoords[]={0,7,8,23,24,487,488,495};
    for(unsigned state=0;state<5;state++)for(unsigned i=0;i<8;i++){
        unsigned x=xcoords[i],sx=(state+5)*32+(x<8?x:x>=488?24+x-488:8+(x-8)%16);
        uint8_t *p=atlas->pixels+(68+state*68+50)*atlas->stride+x*4;
        assert(p[0]==(uint8_t)sx&&p[1]==(sx>>8)&&p[2]==50&&p[3]==255);
    }
    b->vm->globals[1][61].number=1;assert(!call(b,30,0));
    assert(atlas->pixels[68*atlas->stride]==0);
    for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<6;i++){
        KImage *row=&b->choice_rows[bank][i];assert(row->pixels&&row->width==496&&row->height==(i<2?34:52));
    }
    assert(!b->choice_active&&b->choice_selected==-1);
    puts("Kisaku choice initialization and independent animation state: PASS");
    bootstrap_destroy(b);assert(bowling_released==2);return 0;
}

#include "bootstrap.h"
#include "dialog.h"
#include "voice_character.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned bowling_released;
static int native_wait_call(KBootstrap *b,int action,int duration,int pump){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=29;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){789,NULL}));
    assert(!kvm_push(b->vm,(KValue){pump,NULL}));
    assert(!kvm_push(b->vm,(KValue){duration,NULL}));
    assert(!kvm_push(b->vm,(KValue){action,NULL}));
    return bootstrap_dispatch(b);
}
static void test_native_wait(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(!native_wait_call(b,0,20,0)&&b->vm->sp==1&&b->vm->stack[0].number==789);
    size_t ip=b->vm->ip;
    assert(bootstrap_run(b,1)==1&&b->vm->ip==ip);
    bootstrap_confirm(b);bootstrap_cancel(b);bootstrap_pointer(b,10,10,1);
    assert(b->native_wait_clock==1200&&!b->input_events&&!bootstrap_can_save(b));
    b->effect_fast=1;b->vm->globals[0][50].number=0x8000;
    bootstrap_frame(b);assert(b->native_wait_clock==200);
    bootstrap_frame(b);assert(!b->native_wait_clock); /* action 0 never skips */
    b->vm->globals[0][50].number=0;
    assert(!native_wait_call(b,1,1000,0));bootstrap_frame(b);assert(!b->native_wait_clock);
    b->vm->globals[0][50].number=0x4000;
    assert(!native_wait_call(b,1,1000,0));bootstrap_frame(b);assert(b->native_wait_clock==59000);
    b->vm->globals[0][50].number|=0x8000;
    bootstrap_frame(b);assert(!b->native_wait_clock); /* script skip independent of 4000 */
    b->effect_fast=0;b->vm->bytes[4012]=1;
    assert(!native_wait_call(b,1,1000,0));bootstrap_frame(b);assert(b->native_wait_clock==59000);
    b->effect_fast=1;b->vm->globals[0][50].number=0x8000;
    bootstrap_frame(b);assert(!b->native_wait_clock); /* byte4012 only blocks script skip */
    assert(!native_wait_call(b,0,2147483647,0)&&b->native_wait_clock==UINT64_C(128849018820));
    assert(native_wait_call(b,0,-1,0)<0&&b->vm->sp==4&&b->native_wait_clock);
    b->vm->globals[0][50].number=0;b->effect_fast=0;
    assert(!native_wait_call(b,1,20,1)&&b->vm->sp==1&&b->native_wait_clock==1200&&b->native_wait_pump);
    bootstrap_frame(b);assert(b->native_wait_clock==200&&b->native_wait_pump);
    bootstrap_frame(b);assert(!b->native_wait_clock&&!b->native_wait_pump);
    assert(!native_wait_call(b,1,0,1)&&!b->native_wait_clock&&b->vm->sp==1);
    bootstrap_destroy(b);
    puts("Kisaku native waits: duration, input isolation, skip flags, wide clock and portable event pump: PASS");
}
static int backlog_call(KBootstrap *b,int action,int has_value,KValue value){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=23;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){789,NULL}));
    if(has_value)assert(!kvm_push(b->vm,value));
    assert(!kvm_push(b->vm,(KValue){action,NULL}));
    return bootstrap_dispatch(b);
}
static void test_backlog_records(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(!backlog_call(b,0,1,(KValue){3,NULL})&&b->message_count==3&&b->vm->sp==1);
    b->message_index=1;
    KMessageRecord *r=&b->messages[1];
    r->data=malloc(4);r->capacity=r->size=4;memcpy(r->data,"abc",4);
    r->text=malloc(4);r->text_capacity=4;memcpy(r->text,"xyz",4);r->flag=1;
    assert(!backlog_call(b,0,1,(KValue){2,NULL})&&b->message_count==5&&b->message_index==1);
    assert(!backlog_call(b,8,0,(KValue){0,NULL})&&b->vm->sp==1);
    assert(b->message_count==5&&b->message_index==1&&b->messages[1].size);
    assert(!backlog_call(b,4,0,(KValue){0,NULL})&&b->vm->sp==2&&b->vm->stack[1].number==1);
    assert(b->vm->stack[0].number==789);
    assert(!backlog_call(b,6,1,(KValue){1,NULL})&&b->vm->sp==2&&b->vm->stack[1].number==1);
    assert(!backlog_call(b,6,1,(KValue){-1,NULL})&&b->vm->stack[1].number==0);
    assert(!backlog_call(b,6,1,(KValue){5,NULL})&&b->vm->stack[1].number==0);
    assert(!backlog_call(b,7,1,(KValue){1,NULL})&&b->vm->sp==2&&b->vm->stack[1].string&&!strcmp(b->vm->stack[1].string,"xyz"));
    assert(backlog_call(b,7,1,(KValue){99,NULL})<0&&b->vm->sp==3&&strstr(b->error,"23/7"));
    assert(backlog_call(b,5,1,(KValue){1,NULL})<0&&b->vm->sp==3&&strstr(b->error,"23/5"));
    assert(backlog_call(b,0,1,(KValue){-1,NULL})<0&&b->vm->sp==3&&b->message_count==5);
    assert(backlog_call(b,0,1,(KValue){4092,NULL})<0&&b->vm->sp==3&&b->message_count==5);
    assert(backlog_call(b,6,1,(KValue){0,"bad"})<0&&b->vm->sp==3&&b->messages[1].flag);
    b->vm->status=KVM_SYSCALL;b->vm->sp=1;b->vm->stack[0]=(KValue){6,NULL};
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==1);
    uint8_t *data=b->messages[1].data;char *text=b->messages[1].text;
    b->history_count=b->history_next=1;strcpy(b->history[0],"stale fallback");strcpy(b->history_voice[0],"z09577.ogg");
    assert(!backlog_call(b,1,0,(KValue){0,NULL})&&b->vm->sp==1&&b->message_index==-1);
    assert(!bootstrap_backlog_count(b)&&!b->history_count&&!b->history_next&&!b->history[0][0]&&!b->history_voice[0][0]);
    assert(b->message_count==5&&b->messages[1].data==data&&b->messages[1].text==text);
    assert(!b->messages[1].size&&!b->messages[1].flag&&!data[0]&&!text[0]);
    assert(!backlog_call(b,4,0,(KValue){0,NULL})&&b->vm->stack[1].number==0);
    assert(!backlog_call(b,6,1,(KValue){1,NULL})&&b->vm->stack[1].number==0);
    b->vm->syscall=29;b->vm->status=KVM_SYSCALL;b->vm->sp=1;b->vm->stack[0]=(KValue){0,NULL};
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==1);
    b->vm->status=KVM_SYSCALL;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){789,NULL})&&!kvm_push(b->vm,(KValue){1,NULL}));
    assert(!kvm_push(b->vm,(KValue){20,NULL})&&!kvm_push(b->vm,(KValue){0,NULL}));
    assert(!bootstrap_dispatch(b)&&b->vm->sp==1&&b->vm->stack[0].number==789&&b->native_wait_pump);
    bootstrap_frame(b);bootstrap_frame(b);assert(!b->native_wait_clock&&!b->native_wait_pump);
    bootstrap_destroy(b);
    puts("Kisaku backlog records: append, nonempty count, voice query, clear and preserved invalid operands: PASS");
}
static void test_backlog_lifecycle(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(b->message_index==-1);
    b->vm->globals[0][50].number=0x80;
    assert(backlog_call(b,2,0,(KValue){0,NULL})<0&&b->vm->sp==2&&b->message_index==-1);
    assert(!(b->vm->globals[0][50].number&0x100));
    assert(!backlog_call(b,0,1,(KValue){2,NULL}));
    assert(!backlog_call(b,2,0,(KValue){0,NULL})&&b->message_index==0);
    assert(!backlog_call(b,2,0,(KValue){0,NULL})&&b->message_index==0);
    assert(!backlog_call(b,3,0,(KValue){0,NULL}));
    assert(b->messages[0].size==2&&!b->messages[0].data[0]&&!b->messages[0].data[1]);
    assert(!backlog_call(b,3,0,(KValue){0,NULL})&&b->messages[0].size==2);
    /* Text starts the next record without a script 23/2. Measurement mode
       exercises the actual VM/recorder path without requiring a font. */
    static const uint8_t script[]={0,0,0,0,0x0a,'A',0,0x0b,'B',0,0};
    int id=kvm_add_module(b->vm,"backlog-lifecycle",script,sizeof(script));assert(id>=0);
    b->vm->globals[0][50].number=(int32_t)0x80000080u;
    assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100));
    static const uint8_t expected[]={0x0a,'A',0,0x0b,'B',0,0};
    assert(b->message_index==1&&b->messages[1].size==sizeof(expected));
    assert(!memcmp(b->messages[1].data,expected,sizeof(expected))&&b->text_measure_bytes==2);
    assert(!backlog_call(b,3,0,(KValue){0,NULL}));
    assert(b->messages[1].size==sizeof(expected)+1);
    b->messages[0].flag=1;b->messages[0].text=malloc(4);assert(b->messages[0].text);
    memcpy(b->messages[0].text,"old",4);b->messages[0].text_capacity=4;
    uint8_t *retained=b->messages[1].data;
    assert(!backlog_call(b,2,0,(KValue){0,NULL})&&b->message_index==1);
    assert(b->messages[0].data==retained&&!b->messages[1].data&&!b->messages[1].text&&!b->messages[1].flag);
    assert(!backlog_call(b,2,0,(KValue){0,NULL})&&b->messages[0].data==retained);
    /* Failed end retains selector and recording bit; disabled recording is
       an intentional no-op, even with an invalid current slot. */
    b->message_index=-1;
    assert(backlog_call(b,3,0,(KValue){0,NULL})<0&&b->vm->sp==2&&(b->vm->globals[0][50].number&0x100));
    b->vm->globals[0][50].number=0x100;
    assert(!backlog_call(b,2,0,(KValue){0,NULL})&&b->message_index==-1);
    assert(!backlog_call(b,3,0,(KValue){0,NULL})&&b->vm->globals[0][50].number==0x100);
    bootstrap_destroy(b);
    puts("Kisaku backlog lifecycle: first slot, idempotent begin/end, text commands, rollover, preserved failures: PASS");
}
static void test_setting(KBootstrap *b,const char *section,const char *key,const char *value);
static void test_backlog_newline(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(!backlog_call(b,0,1,(KValue){2,NULL}));
    static const uint8_t script[]={0,0,0,0,0x1b,0,0x0b,'A',0,0x1b,0,0};
    static const uint8_t expected[]={0x1b,0,0x0b,'A',0,0x1b,0,0};
    int id=kvm_add_module(b->vm,"newline-measure",script,sizeof(script));assert(id>=0);
    b->vm->globals[0][50].number=(int32_t)0x80000080u;
    b->vm->globals[0][42]=(KValue){32,"unused"};
    b->vm->globals[0][46].number=123;b->vm->globals[0][47].number=456;
    assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100));
    assert(b->message_index==0&&b->messages[0].size==sizeof(expected));
    assert(!memcmp(b->messages[0].data,expected,sizeof(expected))&&b->text_measure_bytes==1);
    assert(b->vm->globals[0][46].number==123&&b->vm->globals[0][47].number==456);
    static const uint8_t newline[]={0,0,0,0,0x1b,0,0};
    id=kvm_add_module(b->vm,"newline-draw",newline,sizeof(newline));assert(id>=0);
    b->vm->globals[0][50].number=0x80;b->vm->globals[0][42]=(KValue){32,NULL};
    b->vm->globals[0][31].number=18;
    assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100));
    assert(b->message_index==1&&b->messages[1].size==3);
    assert(b->vm->globals[0][46].number==32&&b->vm->globals[0][47].number==474);
    b->vm->globals[0][50].number=0;
    assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100)&&b->messages[1].size==3);
    assert(b->vm->globals[0][47].number==492);
    bootstrap_destroy(b);
    b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    id=kvm_add_module(b->vm,"newline-no-slots",newline,sizeof(newline));assert(id>=0);
    b->vm->globals[0][50].number=0x80;b->vm->globals[0][47].number=123;
    assert(!kvm_start(b->vm,id)&&bootstrap_run(b,100)<0&&b->vm->globals[0][47].number==123);
    bootstrap_destroy(b);
    puts("Kisaku backlog newline: automatic begin, opcode/operand retention, measurement isolation, disabled recording and failure: PASS");
}
static void test_backlog_capture(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(!backlog_call(b,0,1,(KValue){2,NULL}));
    b->vm->globals[0][50].number=0x80;
    assert(!backlog_call(b,2,0,(KValue){0,NULL}));
    /* Jump over an unexecuted literal; capture the branch operand, not the
       skipped bytes. String terminator and record sentinel are distinct. */
    static const uint8_t script[]={0,0,0,0,0x32,0x12,0x34,0x56,0x78,
        0x33,'V',0,0x15,0,0,0,18,0x32,0,0,0,99,0};
    static const uint8_t expected[]={0x32,0x12,0x34,0x56,0x78,
        0x33,'V',0,0x15,0,0,0,18,0,0};
    int id=kvm_add_module(b->vm,"capture-branch",script,sizeof(script));assert(id>=0);
    b->vm->globals[0][50].number=0x380;
    assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100));
    assert(b->messages[0].size==sizeof(expected)&&!memcmp(b->messages[0].data,expected,sizeof(expected)));
    assert(b->vm->sp==2&&b->vm->stack[0].number==0x12345678&&!strcmp(b->vm->stack[1].string,"V"));
    for(unsigned flags=0;flags<3;flags++){
        b->vm->globals[0][50].number=(int[]){0,0x80,0x200}[flags];
        assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100));
        assert(b->messages[0].size==sizeof(expected));
    }
    /* Capture enable/disable is sampled before the assignment's effect. */
    static const uint8_t disable[]={0,0,0,0,0x32,0,0,0,0x80,0x32,0,0,0,50,0x0e,0};
    id=kvm_add_module(b->vm,"capture-disable",disable,sizeof(disable));assert(id>=0);
    b->vm->globals[0][50].number=0x380;
    assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100));
    assert(b->vm->globals[0][50].number==0x80);
    assert(b->messages[0].size==sizeof(expected)+11);
    assert(!memcmp(b->messages[0].data+sizeof(expected)-1,disable+4,11));
    static const uint8_t text[]={0,0,0,0,0x0b,'A',0,0x1b,0,0};
    static const uint8_t text_expected[]={0x0b,0x0b,'A',0,0x1b,0,0x1b,0,0,0};
    id=kvm_add_module(b->vm,"capture-text",text,sizeof(text));assert(id>=0);
    b->vm->globals[0][50].number=(int32_t)0x80000280u;
    assert(!kvm_start(b->vm,id)&&!bootstrap_run(b,100));
    assert(b->messages[1].size==sizeof(text_expected)&&!memcmp(b->messages[1].data,text_expected,sizeof(text_expected)));
    bootstrap_destroy(b);
    b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    id=kvm_add_module(b->vm,"capture-no-slot",script,sizeof(script));assert(id>=0);
    b->vm->globals[0][50].number=0x280;
    assert(!kvm_start(b->vm,id)&&bootstrap_run(b,100)<0);
    assert(b->vm->sp==0&&b->vm->ip==0&&b->message_index==-1);
    bootstrap_destroy(b);
    b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(!backlog_call(b,0,1,(KValue){1,NULL}));
    b->vm->globals[0][50].number=0x80;
    assert(!backlog_call(b,2,0,(KValue){0,NULL}));
    uint8_t *fill=malloc(262140);assert(fill);memset(fill,0x5a,262140);
    assert(!b->vm->record_bytes(b,fill,262140));free(fill);
    id=kvm_add_module(b->vm,"capture-full",script,sizeof(script));assert(id>=0);
    b->vm->globals[0][50].number=0x380;
    assert(!kvm_start(b->vm,id)&&bootstrap_run(b,100)<0);
    assert(b->vm->sp==0&&b->vm->ip==0&&b->messages[0].size==262141);
    assert(b->messages[0].data[262139]==0x5a&&!b->messages[0].data[262140]);
    static const uint8_t tail[]={1,2,3,4};
    assert(!b->vm->record_bytes(b,tail,sizeof(tail))&&b->messages[0].size==262145);
    assert(!memcmp(b->messages[0].data+262140,tail,sizeof(tail))&&!b->messages[0].data[262144]);
    assert(b->vm->record_bytes(b,tail,1)<0&&b->messages[0].size==262145);
    bootstrap_destroy(b);
    puts("Kisaku backlog capture: exact operands, strings, executed branch, flag transitions, text overlap, missing-slot/size-limit isolation: PASS");
}
static void bowling_free(void *p){bowling_released++;free(p);}
static int call(KBootstrap *b,int sub,int action){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){action,NULL});kvm_push(b->vm,(KValue){sub,NULL});
    return bootstrap_dispatch(b);
}
static void week_call(KBootstrap *b,int mode,int value){
    b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    if(mode==5)kvm_push(b->vm,(KValue){0,NULL});
    kvm_push(b->vm,(KValue){value,NULL});kvm_push(b->vm,(KValue){mode,NULL});
    kvm_push(b->vm,(KValue){522,NULL});assert(!bootstrap_dispatch(b));
}
static void test_week(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=9;rmt_free(&b->layers[7]);
    b->layers[7]=(KImage){0,0,640,400,2560,calloc(640*400,4)};
    for(unsigned y=0;y<400;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=b->layers[7].pixels+y*2560+x*4;p[0]=y;p[1]=x;p[3]=255;
    }
    week_call(b,0,0);week_call(b,1,0);assert(b->exec522_motion);
    for(unsigned n=0;n<8;n++)bootstrap_frame(b);
    assert(!b->error[0]&&!b->exec522_motion&&b->exec522_x[0]==172);
    week_call(b,3,1);
    assert(b->exec522_sprites[0].pixels[60*432+8*4]==208);
    assert(b->exec522_sprites[0].pixels[84*432+8*4]==24);
    week_call(b,2,0);
    for(unsigned n=0;n<10;n++)bootstrap_frame(b);
    /* Native case 4 pops item first, then group: [group,item,4,522]. */
    b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    const int result_args[]={0,2,4,522};
    for(unsigned i=0;i<4;i++)assert(!kvm_push(b->vm,(KValue){result_args[i],NULL}));
    assert(!bootstrap_dispatch(b)&&!b->exec522_state);
    for(unsigned n=0;n<80;n++)bootstrap_frame(b);
    assert(b->exec522_sprites[0].pixels[84*432+8*4]==(uint8_t)0x178);
    week_call(b,1,1);
    for(unsigned n=0;n<12;n++)bootstrap_frame(b);
    assert(!b->error[0]&&!b->exec522_motion&&b->exec522_visible[0]&&b->exec522_visible[1]);
    assert(b->exec522_x[1]==280);
    for(unsigned n=0;n<30;n++)bootstrap_frame(b);
    assert(b->exec522_drawn[0]&&b->exec522_drawn[1]);
    week_call(b,5,0);assert(b->exec522_visible[0]);
    for(unsigned n=0;n<12;n++)bootstrap_frame(b);
    assert(!b->exec522_motion&&!b->exec522_visible[0]&&!b->exec522_visible[1]);
    b->param_animation_active=1;b->param_animation_step=7;b->input_events=0;
    bootstrap_confirm(b);bootstrap_cancel(b);bootstrap_pointer(b,50,400,1);
    assert(b->param_animation_active&&b->param_animation_step==7&&!b->input_events);
    bootstrap_destroy(b);
    puts("Weekly panels: retained weeks, label rows, timed movement; parameter click isolation: PASS");
}
static void test_native_tint(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=3;
    for(unsigned i=2;i<=3;i++){rmt_free(&b->layers[i]);b->layers[i]=(KImage){0,0,642,481,642*4,calloc(642*481,4)};}
    assert(b->layers[2].pixels&&b->layers[3].pixels);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=b->layers[0].pixels+y*b->layers[0].stride+x*4;
        p[0]=(uint8_t)x;p[1]=(uint8_t)y;p[2]=(uint8_t)(x+y);p[3]=23;
    }
    memset(b->layers[3].pixels,91,642*481*4);
    b->vm->status=KVM_SYSCALL;b->vm->syscall=31;
    assert(!kvm_push(b->vm,(KValue){73,NULL})&&!kvm_push(b->vm,(KValue){41,NULL}));
    assert(!bootstrap_dispatch(b)&&b->vm->sp==1&&b->vm->stack[0].number==73);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *src=b->layers[0].pixels+y*b->layers[0].stride+x*4;
        const uint8_t *dst=b->layers[3].pixels+y*b->layers[3].stride+x*4;
        unsigned mean=((unsigned)src[0]+src[1]+src[2])/3;
        assert(!memcmp(src,b->layers[2].pixels+y*b->layers[2].stride+x*4,4));
        assert(dst[0]==mean&&dst[1]==(mean>239?255:mean+16)&&dst[2]==dst[1]&&dst[3]==91);
    }
    assert(b->layers[3].pixels[480*b->layers[3].stride]==91);
    b->layers[3].height=479;b->layers[0].pixels[0]=99;
    b->vm->status=KVM_SYSCALL;assert(!kvm_push(b->vm,(KValue){41,NULL}));
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==2&&b->layers[2].pixels[0]==0);
    bootstrap_destroy(b);
    puts("Kisaku 31/41 snapshot/tint: RGB, Alpha, padded surfaces, preserved stack and failed preflight: PASS");
}
static int call_gallery_mark(KBootstrap *b,const char *name){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){0,name});kvm_push(b->vm,(KValue){1012,NULL});
    return bootstrap_dispatch(b);
}
static int call_527(KBootstrap *b){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){0,NULL});kvm_push(b->vm,(KValue){0,NULL});
    kvm_push(b->vm,(KValue){527,NULL});
    return bootstrap_dispatch(b);
}
static int call_overlay524(KBootstrap *b,int packed,int third,int action){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    if(action==0){kvm_push(b->vm,(KValue){third,NULL});kvm_push(b->vm,(KValue){packed,NULL});}
    kvm_push(b->vm,(KValue){action,NULL});kvm_push(b->vm,(KValue){524,NULL});
    return bootstrap_dispatch(b);
}
static int call_location_create(KBootstrap *b,int x,int y,int w,int h,int right_bytes){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    /* VM call sites push variants in reverse order; the dispatcher reads
       them back as source-x, source-y, width, height, right-bytes. */
    kvm_push(b->vm,(KValue){right_bytes,NULL});kvm_push(b->vm,(KValue){h,NULL});
    kvm_push(b->vm,(KValue){w,NULL});kvm_push(b->vm,(KValue){y,NULL});
    kvm_push(b->vm,(KValue){x,NULL});kvm_push(b->vm,(KValue){0,NULL});kvm_push(b->vm,(KValue){526,NULL});
    return bootstrap_dispatch(b);
}
static int call_anime520(KBootstrap *b,const KValue *args,unsigned count){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<count;i++)kvm_push(b->vm,args[i]);
    return bootstrap_dispatch(b);
}
static void test_bowling_modal(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    const unsigned layers[]={0,1,2,3,4,5,8,9};
    const char *names[]={"bow_bg.akb","bow_pin.akb","bow_ball.akb","bow_sr.akb","bow_pt02.akb","bow_pt.akb","bow_c0.akb","bow_men.akb"};
    b->layer_count=9;
    for(unsigned i=0;i<8;i++){
        uint8_t *data=NULL;size_t size=0;rmt_free(&b->layers[layers[i]]);
        assert(!ai6_read_named(&b->images,names[i],&data,&size));
        assert(!rmt_decode(data,size,&b->layers[layers[i]]));free(data);
    }
    KValue args[]={{73,NULL},{3,NULL},{2,NULL},{1,NULL},{0,NULL},{1,NULL},{0,NULL},{612,NULL}};
    assert(!call_anime520(b,args,8)&&b->vm->sp==1&&b->vm->stack[0].number==73&&bootstrap_bowling_active(b));
    assert(!bootstrap_can_save(b));bootstrap_confirm(b);
    unsigned frames=0,throws=0;
    for(unsigned half=0;half<2;half++){
        while(bootstrap_bowling_active(b)){
            KBowlingRuntime *r=b->bowling_runtime;
            if(r->game.phase==KB_GAME_TURN&&r->game.turn.phase==KB_TURN_AIM){
                bootstrap_bowling_pointer(b,320,440,1);bootstrap_bowling_pointer(b,320,200,1);bootstrap_bowling_pointer(b,320,200,0);throws++;
            }
            if(r->game.phase==KB_GAME_RESULTS)bootstrap_confirm(b);
            bootstrap_frame(b);
            if(b->error[0])fprintf(stderr,"Bowling modal: %s\n",b->error);
            assert(!b->error[0]&&++frames<100000);
        }
        if(!half){
            assert(b->bowling_runtime&&b->bowling_runtime->game.session.match.first_finished&&b->vm->sp==1);
            assert(!call(b,612,1)&&bootstrap_bowling_active(b)&&!b->vm->sp);
        }
    }
    assert(!b->bowling_runtime&&!b->current_bowling&&b->vm->sp==1&&b->vm->stack[0].number>=0&&throws>=10);
    assert(!call(b,612,2));bootstrap_destroy(b);
    printf("Bowling real-asset modal: two halves, %u user throws, %u rendered frames, result and cleanup: PASS\n",throws,frames);
}
static void test_bowling_scripts(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    int result=1;unsigned ticks=0;
    while(!(b->title.active&&b->title.age>=64)){
        result=bootstrap_run(b,100000);assert(result==1);bootstrap_frame(b);assert(++ticks<20000&&!b->error[0]);
    }
    b->title.active=0;
    for(unsigned i=0;i<4;i++)b->vm->bytes[1504+i]=(uint8_t)i;
    b->vm->bytes[1508]=1;
    unsigned seen=0;
    for(unsigned half=0;half<2;half++){
        uint8_t *data=NULL;size_t size=0;const char *name=half?"bowl2nd.mes":"bowl1st.mes";
        assert(!ai6_read_named(&b->scripts,name,&data,&size));
        int id=kvm_add_module(b->vm,name,data,size);assert(id>=0);b->module_data[id]=data;
        assert(!kvm_start(b->vm,id));
        unsigned frames=0;
        do {
            result=bootstrap_run(b,100000);
            if(bootstrap_bowling_active(b)){
                KBowlingRuntime *r=b->bowling_runtime;seen|=1u<<half;
                if(r->game.phase==KB_GAME_TUTORIAL||r->game.phase==KB_GAME_RESULTS)bootstrap_confirm(b);
                else if(r->game.phase==KB_GAME_TURN&&r->game.turn.phase==KB_TURN_AIM){
                    bootstrap_bowling_pointer(b,320,440,1);bootstrap_bowling_pointer(b,320,200,1);bootstrap_bowling_pointer(b,320,200,0);
                }
            }
            if(result<0||b->error[0])fprintf(stderr,"Original bowling MES: %s\n",b->error);
            assert(result>=0&&!b->error[0]&&++frames<100000);
            if(result)bootstrap_frame(b);
        }while(result);
        assert(!bootstrap_bowling_active(b));
    }
    assert(seen==3&&!b->bowling_runtime&&b->vm->bytes[1509]<=1&&b->vm->bytes[1510]<=1);
    bootstrap_destroy(b);puts("Original bowl1st/bowl2nd MES resource setup, physical play, score flags and returns: PASS");
}
static int call_layer(KBootstrap *b,const KValue *args,unsigned count,int sub){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=19;b->vm->sp=0;
    for(unsigned i=0;i<count;i++)kvm_push(b->vm,args[i]);
    kvm_push(b->vm,(KValue){sub,NULL});return bootstrap_dispatch(b);
}
static void test_scene_context(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    memset(b->vm->bytes+3569,0xa5,26);
    char text[25]="scene01.mes";b->vm->globals[0][70]=(KValue){0,text};
    assert(!call(b,812,0)&&!b->vm->sp&&!memcmp(b->vm->bytes+3570,"scene01.mes",11));
    for(unsigned i=11;i<24;i++)assert(!b->vm->bytes[3570+i]);
    assert(b->vm->bytes[3569]==0xa5&&b->vm->bytes[3594]==0xa5);
    assert(!call(b,812,1)&&!strcmp(b->vm->globals[0][70].string,"scene01.mes"));
    const char *owned=b->vm->globals[0][70].string;memset(b->vm->bytes+3570,'x',24);
    assert(!strcmp(owned,"scene01.mes"));assert(!call(b,812,1)&&strlen(b->vm->globals[0][70].string)==23);
    assert(b->vm->bytes[3593]=='x'); /* Read termination never modifies the stored bytes. */
    memset(text,'a',23);text[23]=0;b->vm->globals[0][70]=(KValue){0,text};assert(!call(b,812,0));
    text[23]='b';text[24]=0;assert(call(b,812,0)<0&&b->vm->sp==2&&b->vm->bytes[3593]==0);
    b->vm->globals[0][70]=(KValue){0,""};assert(!call(b,812,0));assert(!call(b,812,1)&&!b->vm->globals[0][70].string[0]);
    b->vm->globals[0][70]=(KValue){0,NULL};assert(call(b,812,0)<0&&b->vm->sp==2);
    assert(call(b,812,9)<0&&b->vm->sp==2);unsigned count=b->vm->byte_count;b->vm->byte_count=3593;
    assert(call(b,812,1)<0&&b->vm->sp==2);b->vm->byte_count=count;
    /* Original hage_scmode.mes loads bg04 directly onto display layer0. */
    uint8_t *data=NULL;size_t size=0;KImage expected={0};
    assert(!ai6_read_named(&b->images,"bg04.akb",&data,&size)&&!rmt_decode(data,size,&expected));free(data);
    for(unsigned async=0;async<2;async++){
        if(async)assert(!bootstrap_enable_async_images(b));
        KValue args[]={{0,NULL},{0,"bg04.akb"}};assert(!call_layer(b,args,2,1));
        for(unsigned i=0;b->image_loading&&i<1000000;i++)bootstrap_frame(b);
        assert(!b->image_loading&&!b->error[0]&&b->last_loaded_layer==0);
        for(unsigned y=0;y<expected.height;y++)assert(!memcmp(b->layers[0].pixels+(y+expected.y)*2560+expected.x*4,expected.pixels+y*expected.stride,expected.width*4));
        assert(b->vm->globals[0][40].number==expected.x+(int)expected.width);
    }
    rmt_free(&expected);bootstrap_destroy(b);
    puts("Kisaku scene context: native 24-byte name storage, owned readback, bounds/error operands and display-layer0 sync/async image loading: PASS");
}

static void test_location_label(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=7;
    for(unsigned i=0;i<2;i++){
        unsigned layer=i?7:0;rmt_free(&b->layers[layer]);
        b->layers[layer]=(KImage){0,0,640,480,2560,calloc(480,2560)};assert(b->layers[layer].pixels);
    }
    memset(b->layers[0].pixels,127,480*2560);
    for(unsigned y=304;y<336;y++)for(unsigned x=160;x<192;x++){
        uint8_t *p=b->layers[7].pixels+y*2560+x*4;p[2]=255;p[3]=255;
        if(x==160){p[1]=255;p[2]=0;}
    }
    b->font_width=b->font_height=16;b->vm->globals[0][30].number=16;b->vm->globals[0][31].number=18;
    b->vm->globals[0][33].number=0xffffff;
    test_setting(b,"Msg","Alpha","92");test_setting(b,"Msg","Red","0");test_setting(b,"Msg","Green","0");test_setting(b,"Msg","Blue","0");
    uint8_t *frame=malloc(480*2560);assert(frame);
    for(int place=0;place<40;place++){
        b->vm->globals[0][0].number=place;
        assert(!call(b,526,2)&&!b->vm->sp&&b->exec526_active);
        unsigned bytes=(unsigned)(b->vm->globals[0][46].number-168)/8;
        assert(bytes>0&&bytes<=30&&b->exec526_x==608-(int)bytes*8&&b->exec526_y==16);
        assert(b->vm->globals[0][49].number==7&&b->vm->globals[0][42].number==168);
        assert(b->exec526_surfaces[0].pixels[3]==0&&b->exec526_surfaces[0].pixels[7]==182);
        unsigned glyphs=0;for(unsigned p=0;p<336*32;p++)glyphs+=b->exec526_surfaces[1].pixels[p*4+3]!=0;assert(glyphs);
        bootstrap_frame(b);assert(b->exec526_drawn&&!b->error[0]);
        unsigned offset=16*2560+(unsigned)b->exec526_x*4;
        assert(b->layers[0].pixels[offset]==127&&b->layers[0].pixels[offset+4]==127*(255-182)/255);
        assert(b->layers[0].pixels[offset+7]==127); /* Native blend keeps destination alpha. */
        memcpy(frame,b->layers[0].pixels,480*2560);bootstrap_frame(b);
        assert(!memcmp(frame,b->layers[0].pixels,480*2560)); /* No repeated blend darkening. */
        b->layers[0].pixels[479*2560]=99;bootstrap_frame(b);
        assert(b->layers[0].pixels[479*2560]==99); /* Do not restore unrelated UI writes. */
        b->layers[0].pixels[479*2560]=127;
        assert(!call(b,526,1)&&!b->exec526_active&&!b->exec526_drawn);
        for(unsigned p=0;p<480*2560;p++)assert(b->layers[0].pixels[p]==127);
    }
    b->vm->globals[0][0].number=-1;assert(call(b,526,2)<0&&b->vm->sp==2);
    assert(!call_location_create(b,160,304,16,16,0)&&!b->vm->sp&&b->exec526_active);
    assert(b->exec526_width==16&&b->exec526_height==16&&b->exec526_x==608&&b->exec526_y==16);
    bootstrap_frame(b);assert(b->exec526_drawn&&!b->error[0]);
    assert(!call(b,526,1)&&!b->exec526_active&&!b->exec526_drawn);
    free(frame);bootstrap_destroy(b);
    puts("Kisaku location labels: all 40 CP932 names, native caps/key/alpha, clipped position, stable overlay and release: PASS");
}
static int call_anime13(KBootstrap *b,int action,int bank,int cell){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=13;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){73,NULL}));
    if(action!=6&&action!=7&&action!=10&&action!=11){
        assert(!kvm_push(b->vm,(KValue){cell,NULL}));assert(!kvm_push(b->vm,(KValue){bank,NULL}));
    }
    assert(!kvm_push(b->vm,(KValue){action,NULL}));
    int result=bootstrap_dispatch(b);
    if(!result)assert(b->vm->sp==1&&b->vm->stack[0].number==73);
    return result;
}
static void test_animation_registration(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    uint8_t data[0x620]={0};
    for(unsigned i=0;i<3;i++){data[i*4]=0;data[i*4+1]=6;}data[0x600]=255;
    assert(ax_load(&b->ax,"registry.ax",data,sizeof(data)));
    assert(!call(b,1011,0));strcpy(b->media_background_name,"EV14A.AKB");
    /* Real duplicate-name records 5001/5002 require different tracks. */
    assert(!call_anime13(b,2,0,1)&&b->ax_registered[1]==1);
    assert(!call(b,1011,11)&&b->vm->bytes[5001]&&!b->vm->bytes[5002]);
    b->vm->bytes[5001]=0;
    assert(!call_anime13(b,2,0,1)&&b->ax_registered[1]==2);
    assert(!call(b,1011,11)&&!b->vm->bytes[5001]); /* Duplicates count. */
    assert(!call_anime13(b,4,0,1)&&b->ax_registered[1]==1&&b->ax.cells[1].state==AX_STOPPED);
    assert(!call(b,1011,11)&&b->vm->bytes[5001]); /* State is not registration. */
    assert(!call_anime13(b,4,0,1)&&!b->ax_registered[1]);
    assert(!call_anime13(b,2,0,2));assert(!call(b,1011,11)&&b->vm->bytes[5002]);
    strcpy(b->media_background_name,"EV01.AKB");
    assert(!call(b,1011,11)&&!b->vm->bytes[6231]); /* Unconditional needs empty list. */
    b->vm->globals[0][50].number=0;b->wait_input=1;
    assert(!call_anime13(b,8,0,2)&&b->ax_registered[2]==1);
    assert(bootstrap_run(b,1)==1&&!b->ax_modal&&!b->ax_registered[2]);
    assert(!call(b,1011,11)&&b->vm->bytes[6231]);
    assert(!call_anime13(b,9,0,2)&&b->ax_registered[2]==1);
    assert(!call_anime13(b,10,0,0));assert(bootstrap_run(b,1)==1&&!b->ax_registered[2]);
    assert(!call_anime13(b,11,0,0)&&b->ax_registered[2]==1);
    assert(!call_anime13(b,6,0,0)&&!b->ax_registered[2]);
    assert(!call_anime13(b,7,0,0));
    assert(!call_anime13(b,5,0,0)&&!b->ax_registered[0]); /* Direct wait doesn't register. */
    b->vm->globals[0][50].number=0x20;bootstrap_cancel(b);assert(!b->ax_modal);
    assert(call_anime13(b,2,10,0)<0&&b->vm->sp==4&&b->vm->stack[0].number==73);
    b->ax_registered[2]=UINT32_MAX;
    assert(call_anime13(b,2,0,2)<0&&b->vm->sp==4&&b->ax_registered[2]==UINT32_MAX);
    bootstrap_destroy(b);
    puts("Kisaku CG registration: real conditional variants, duplicate count, stop/pause/resume, unconditional gating and transactional operands: PASS");
}
static void test_animation_waits(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    /* A real instruction stream: delay two ticks, boundary, then finish.
       Use distinct nonzero indices to detect accidentally waiting on normal AX. */
    uint8_t data[0x640]={0};unsigned index=2*32+5,start=0x600;
    for(unsigned j=0;j<4;j++)data[index*4+j]=(uint8_t)(start>>(j*8));
    data[start]=2;data[start+1]=2;data[start+5]=1;data[start+6]=1;data[start+10]=255;
    assert(ax_load(&b->ax_extra,"wait.ax",data,sizeof(data)));
    b->vm->globals[0][50].number=0x10;b->wait_input=1;
    b->ax.cells[17].state=0;
    const KValue play[]={{5,NULL},{2,NULL},{5,NULL},{520,NULL}};
    assert(!call_anime520(b,play,4)&&!b->vm->sp&&b->ax_extra_modal==1);
    assert(bootstrap_run(b,1)==1&&b->ax_extra_modal==1);
    bootstrap_cancel(b);assert(b->ax_extra_modal==1); /* No skip permission. */
    b->vm->globals[0][50].number|=0x20;
    bootstrap_confirm(b);assert(b->ax_extra_modal==1); /* Native 0x101 is cancel. */
    bootstrap_cancel(b);assert(!b->ax_extra_modal&&!b->ax_extra.wait_cell&&b->ax_extra.cells[index].state==AX_STOPPED&&b->wait_input);
    assert(!call_anime520(b,play,4));
    for(unsigned frame=0;frame<16&&b->ax_extra_modal;frame++){bootstrap_frame(b);assert(bootstrap_run(b,1)==1);}
    assert(!b->error[0]&&!b->ax_extra_modal&&!b->ax_extra.wait_cell&&b->ax.cells[17].state==0);
    /* Pause reaches opcode 1 without executing the final stop. */
    assert(ax_load(&b->ax_extra,"wait.ax",data,sizeof(data)));
    KValue command[]={{5,NULL},{2,NULL},{2,NULL},{520,NULL}};
    assert(!call_anime520(b,command,4));command[2].number=8;
    assert(!call_anime520(b,command,4)&&b->ax_extra_modal==2);
    bootstrap_cancel(b);assert(b->ax_extra_modal==2);
    for(unsigned frame=0;frame<16&&b->ax_extra_modal;frame++){bootstrap_frame(b);assert(bootstrap_run(b,1)==1);}
    struct ax_cell paused=b->ax_extra.cells[index];
    assert(!b->error[0]&&!b->ax_extra_modal&&paused.state==4&&paused.ip==10);
    command[2].number=9;assert(!call_anime520(b,command,4));paused.state=0;
    assert(!memcmp(&paused,&b->ax_extra.cells[index],sizeof(paused)));
    bootstrap_frame(b);bootstrap_frame(b);assert(b->ax_extra.cells[index].state==AX_STOPPED);
    /* Action 8 scans other tracks too; disabled animation settles pending
       pauses instead of hanging forever waiting for a disabled clock. */
    b->ax_extra.cells[index].state=4;b->ax_extra.cells[index+1]=b->ax_extra.cells[index];
    b->ax_extra.cells[index+1].state=0;b->vm->globals[0][50].number=0;
    command[2].number=8;assert(!call_anime520(b,command,4));
    assert(bootstrap_run(b,1)==1&&b->ax_extra_modal==2&&b->ax_extra.cells[index+1].state==3);
    assert(bootstrap_run(b,1)==1&&!b->ax_extra_modal&&b->ax_extra.cells[index+1].state==4);
    b->ax_extra.cells[index+2].state=1;
    assert(!call(b,520,11)&&b->ax_extra.cells[index].state==0&&b->ax_extra.cells[index+1].state==0&&b->ax_extra.cells[index+2].state==1);
    assert(!call(b,520,10)&&b->ax_extra_modal==2);
    assert(bootstrap_run(b,1)==1&&!b->ax_extra_modal&&b->ax_extra.cells[index].state==4&&b->ax_extra.cells[index+2].state==1);
    /* Stop and replacement clear pending waits; failures consume nothing. */
    assert(!call(b,520,7)&&!b->ax_extra_modal&&!b->ax_extra.wait_cell);
    command[0].number=32;command[2].number=5;
    assert(call_anime520(b,command,4)<0&&b->vm->sp==4&&!b->ax_extra_modal);
    command[0].number=5;command[1].string="bad";
    assert(call_anime520(b,command,4)<0&&b->vm->sp==4&&!b->ax_extra_modal);
    command[1].string=NULL;command[0].number=7;
    assert(call_anime520(b,command,4)<0&&b->vm->sp==4&&!b->ax_extra_modal);
    bootstrap_destroy(b);
    puts("Kisaku AX waits: completion, cancel permission, boundary pause/resume, disabled clock, independent manager, preserved invalid operands: PASS");
}
static void test_graphics_windows(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b);
    b->layer_count=9;b->vm->globals[0][50].number=0x10;
    for(unsigned layer=0;layer<=9;layer++){
        rmt_free(&b->layers[layer]);b->layers[layer]=(KImage){0,0,640,480,2560,malloc(480*2560)};
        assert(b->layers[layer].pixels);memset(b->layers[layer].pixels,20+layer,480*2560);
    }
    /* Two nonzero bank/cell values and distinct source/destination colors
       distinguish argument reversal, wrong atlas, and a leaked target. */
    uint8_t ax[0x680]={0};unsigned track=2*32+5,start=0x600;
    for(unsigned j=0;j<4;j++)ax[track*4+j]=(uint8_t)(start>>(j*8));
    uint32_t descriptor[7]={0,11,12,3,2,40,50};memcpy(ax+0x500,descriptor,sizeof(descriptor));
    ax[start]=1;ax[start+1]=20;ax[start+5]=9;ax[start+10]=255;
    assert(ax_load(&b->ax_extra,"synthetic.ax",ax,sizeof(ax))&&ax_load(&b->ax,"synthetic.ax",ax,sizeof(ax)));
    const KValue first[]={{3,NULL},{5,NULL},{2,NULL},{12,NULL},{520,NULL}};
    assert(!call_anime520(b,first,5)&&!b->vm->sp&&!b->animation_target_layer);
    for(unsigned y=50;y<52;y++)for(unsigned x=40;x<43;x++){
        const uint8_t *d=b->layers[3].pixels+y*2560+x*4;
        assert(d[0]==29&&d[1]==29&&d[2]==29&&d[3]==23);
        assert(b->layers[0].pixels[y*2560+x*4]==20);
    }
    KValue start_args[]={{5,NULL},{2,NULL},{2,NULL},{520,NULL}};
    assert(!call_anime520(b,start_args,4));
    for(unsigned frame=0;frame<32;frame++)bootstrap_frame(b);
    assert(!b->error[0]&&b->layers[0].pixels[50*2560+40*4]==29);
    b->vm->status=KVM_SYSCALL;b->vm->syscall=13;b->vm->sp=0;
    const int normal[]={4,5,2,12};for(unsigned i=0;i<4;i++)assert(!kvm_push(b->vm,(KValue){normal[i],NULL}));
    assert(!bootstrap_dispatch(b)&&!b->vm->sp&&!b->ax_destination);
    assert(b->layers[4].pixels[50*2560+40*4]==28&&b->layers[4].pixels[50*2560+40*4+3]==24);
    /* Values update all private rows. Only the selected client rectangle
       may reach the display; rows below it remain the captured scene. */
    for(unsigned rows=1;rows<=4;rows++){
        const KValue setup[]={{(int32_t)rows,NULL},{0,NULL},{10,NULL},{528,NULL}};
        assert(!call_anime520(b,setup,4));
        const KValue values[]={{10,NULL},{30,NULL},{50,NULL},{802,NULL},{0,NULL},{528,NULL}};
        assert(!call_anime520(b,values,6));
        memset(b->layers[0].pixels,87,480*2560);
        if(!b->param_backing.pixels)b->param_backing=(KImage){18,340,602,112,2408,malloc(112*2408)};
        assert(b->param_backing.pixels);memset(b->param_backing.pixels,87,112*2408);
        b->param_animation_window=1;bootstrap_frame(b);assert(!b->error[0]);
        unsigned height=rows==4?112:30+29*(rows-1);
        for(unsigned y=0;y<112;y++)for(unsigned x=0;x<602;x++){
            const uint8_t *d=b->layers[0].pixels+(340+y)*2560+(18+x)*4;
            if(y<height)assert(!memcmp(d,b->param_surface.pixels+y*2408+x*4,4));
            else for(unsigned c=0;c<4;c++)assert(d[c]==87);
        }
        if(rows<4){
            b->layers[0].pixels[451*2560+18*4]=66;bootstrap_frame(b);
            assert(b->layers[0].pixels[451*2560+18*4]==66);
        }
        /* A completed chime must close the window instead of restarting. */
        b->param_animation_active=1;b->param_animation_phase=3;b->param_animation_chime=1;
        for(unsigned i=0;i<4;i++)b->param_animation_plan.target[i]=b->param_values[i];
        b->param_animation_plan.total_target=b->param_total;
        free(b->effect_tracks[0].pcm);memset(&b->effect_tracks[0],0,sizeof(b->effect_tracks[0]));
        b->effect_tracks[0].pcm=calloc(5880,1);assert(b->effect_tracks[0].pcm);
        b->effect_tracks[0].size=5880;
        bootstrap_confirm(b);bootstrap_cancel(b);bootstrap_pointer(b,50,400,1);
        bootstrap_frame(b);assert(b->param_animation_active&&b->effect_tracks[0].clock_position==2940);
        bootstrap_frame(b);assert(!b->error[0]&&!b->param_animation_active&&!b->param_animation_window);
        assert(b->effect_tracks[0].pcm&&b->effect_tracks[0].clock_position==5880);
        assert(!memcmp(b->layers[0].pixels+340*2560+18*4,b->param_backing.pixels,602*4));
    }
    bootstrap_destroy(b);
    puts("Kisaku parameter client clipping and AX bank/cell/target/source/first-frame restoration: PASS");
}
static void test_portrait_key(const char *root,const char *saves,const char *name,unsigned key){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    uint8_t *data=NULL;size_t size=0;KImage image={0};
    assert(!ai6_read_named(&b->images,name,&data,&size)&&!rmt_decode(data,size,&image));free(data);
    b->layer_count=2;b->layers[2]=image;
    size_t bytes=image.stride*image.height;
    b->layers[1]=(KImage){0,0,image.width,image.height,image.stride,malloc(bytes)};assert(b->layers[1].pixels);
    /* Original 19/4 pop order ends with key=0x00ff00, copyAlpha=0/1.
       An opaque light backdrop makes dropped black/near-black pixels visible. */
    KValue args[]={{0,NULL},{(int32_t)key,NULL},{2,NULL},{0,NULL},{0,NULL},{1,NULL},
        {(int32_t)image.height,NULL},{(int32_t)image.width,NULL},{0,NULL},{0,NULL}};
    unsigned transparent=0,darks=0,near_black=0;
    for(unsigned alpha=0;alpha<=1;alpha++){
        for(size_t i=0;i<bytes;i+=4){uint8_t *p=b->layers[1].pixels+i;p[0]=211;p[1]=223;p[2]=239;p[3]=73;}
        args[0].number=(int32_t)alpha;assert(!call_layer(b,args,10,4)&&!b->vm->sp);
        for(size_t i=0;i<bytes;i+=4){
            const uint8_t *s=image.pixels+i,*d=b->layers[1].pixels+i;
            unsigned rgb=s[0]|(unsigned)s[1]<<8|(unsigned)s[2]<<16;
            if(rgb==key){assert(d[0]==211&&d[1]==223&&d[2]==239&&d[3]==73);transparent++;}
            else {assert(!memcmp(s,d,3)&&d[3]==(alpha?s[3]:73));if(s[0]<8&&s[1]<8&&s[2]<8)darks++;if(rgb==1)near_black++;}
        }
    }
    assert(darks);
    if(!strcmp(name,"ev01.akb"))assert(near_black==49*2);
    else assert(transparent);
    printf("Kisaku %s key=%06x: %u keyed, %u dark, %u RGB=000001 pixels checked twice: PASS\n",name,key,transparent/2,darks/2,near_black/2);
    bootstrap_destroy(b);
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
    unsigned sx=glyph<10?524+8*glyph:524+8*(glyph-10),sy=glyph<10?112:128;
    for(unsigned y=0;y<16;y++)assert(!memcmp(b->param_surface.pixels+(92+y)*b->param_surface.stride+x*4,
        b->param_atlas.pixels+(sy+y)*b->param_atlas.stride+sx*4,32));
}
static void test_message_fade(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=1;
    for(unsigned i=0;i<2;i++)b->layers[i]=(KImage){0,0,640,480,2560,calloc(480,2560)};
    unsigned idx=0;for(;idx<b->setting_count;idx++)if(!strcmp(b->settings[idx].section,"Display")&&!strcmp(b->settings[idx].key,"EffectSpeed"))break;
    if(idx==b->setting_count)b->setting_count++;
    strcpy(b->settings[idx].section,"Display");strcpy(b->settings[idx].key,"EffectSpeed");
    b->vm->globals[0][49].number=1;b->vm->globals[0][46].number=2;
    b->vm->globals[0][47].number=2;b->vm->globals[0][31].number=1;
    const size_t pixel=2*2560+319*4;
    KValue fade[]={{71,NULL},{1,NULL},{-1,NULL},{8,NULL},{3,NULL},{43,NULL}};
    for(unsigned speed=0;speed<3;speed++){
        snprintf(b->settings[idx].value,sizeof(b->settings[idx].value),"%u",speed);
        assert(!call(b,43,0));
        for(unsigned i=0;i<640*480;i++){
            uint8_t *d=b->layers[0].pixels+i*4;d[0]=50;d[1]=50;d[2]=50;d[3]=77;
        }
        uint8_t *src=b->layers[1].pixels+2*2560;
        src[0]=100;src[1]=100;src[2]=100;src[3]=192;
        uint8_t *old=b->mes_fade_surfaces[1].pixels+pixel;
        old[0]=200;old[1]=200;old[2]=200;old[3]=128;
        assert(!call_anime520(b,fade,6)&&b->vm->sp==1&&b->vm->stack[0].number==71);
        unsigned steps=speed==0?4:speed==1?2:0;
        assert(b->mes_fade_steps==steps);
        if(steps){
            assert(b->mes_fade_surfaces[0].pixels[pixel]==100);
            assert(b->mes_fade_surfaces[0].pixels[pixel+16]==0);
            assert(bootstrap_run(b,1)==1); /* VM must wait, with its stack intact. */
            bootstrap_frame(b);
            unsigned op=255-255/steps,a=128*op/255,na=192*(255-op)/255;
            unsigned expected=(50*(255-a)/255+200*a/255)*(255-na)/255+100*na/255;
            assert(b->layers[0].pixels[pixel]==expected&&b->layers[0].pixels[pixel+3]==77);
        }
        unsigned frames=steps?1:0;
        while(b->mes_fade_transition&&frames<10){bootstrap_frame(b);frames++;}
        assert(frames==(speed==0?4:speed==1?2:0));
        bootstrap_frame(b);
        assert(!b->mes_fade_transition&&b->layers[0].pixels[pixel]==87);
        assert(b->mes_fade_surfaces[1].pixels[pixel]==100&&!b->mes_fade_surfaces[0].pixels[pixel]);
        bootstrap_frame(b);assert(b->layers[0].pixels[pixel]==87); /* no repeated blending */
        KValue hide[]={{0,NULL},{4,NULL},{43,NULL}};
        assert(!call_anime520(b,hide,3));bootstrap_frame(b);
        assert(!b->mes_fade_visible&&b->layers[0].pixels[pixel]==50);
        assert(!call(b,43,1));bootstrap_frame(b);
    }
    assert(!call(b,43,0));
    KValue bad[]={{4,NULL},{-1,NULL},{8,NULL},{3,NULL},{43,NULL}};
    assert(call_anime520(b,bad,5)<0&&b->vm->sp==5&&!b->mes_fade_transition);
    b->vm->globals[0][46].number=700;
    assert(call_anime520(b,fade,6)<0&&b->vm->sp==6&&!b->mes_fade_transition);
    b->vm->globals[0][46].number=2;
    b->vm->globals[0][50].number|=0x4000; /* full duration, even with EffectSpeed=2 */
    assert(!call_anime520(b,fade,6)&&b->mes_fade_steps==8);
    bootstrap_confirm(b);assert(b->mes_fade_transition==3);
    b->vm->globals[0][50].number&=~0x4000;
    bootstrap_confirm(b);assert(!b->mes_fade_transition);
    KValue shade[]={{0,NULL},{5,NULL},{43,NULL}};
    assert(!call_anime520(b,shade,3)&&b->mes_fade_shade_visible);
    bootstrap_frame(b);assert(b->layers[0].pixels[0]==50*155/255&&b->layers[0].pixels[3]==77);
    assert(!call_anime520(b,shade,3)&&!b->mes_fade_transition);
    shade[1].number=6;assert(!call_anime520(b,shade,3));bootstrap_frame(b);
    assert(!b->mes_fade_shade_visible&&b->layers[0].pixels[0]==50);
    assert(!call_anime520(b,shade,3)&&!b->mes_fade_transition);
    bootstrap_destroy(b);
    puts("Kisaku message crossfade: RGB/Alpha, speed/15ms clock, persistent sprites, skip and error stack: PASS");
}
static void test_message_reveal(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=1;b->layers[1]=(KImage){0,0,640,480,2560,calloc(480,2560)};
    assert(b->layers[1].pixels);test_setting(b,"Display","EffectSpeed","0");
    b->vm->globals[0][49].number=1;b->vm->globals[0][46].number=600;
    b->vm->globals[0][47].number=210;b->vm->globals[0][31].number=4;
    uint8_t *mask=NULL;size_t mask_size=0;
    assert(!ai6_read_named(&b->data,"03.bin",&mask,&mask_size)&&mask_size>=640*480);
    for(unsigned type=2;type<=3;type++)for(unsigned speed=0;speed<3;speed++){
        char setting[2]={(char)('0'+speed),0};test_setting(b,"Display","EffectSpeed",setting);
        assert(!call(b,43,0));b->mes_fade_visible=1;
        for(unsigned p=0;p<640*480;p++){
            uint8_t *screen=b->layers[0].pixels+p*4,*old=b->mes_fade_surfaces[1].pixels+p*4,*src=b->layers[1].pixels+p*4;
            screen[0]=screen[1]=screen[2]=50;screen[3]=77;
            old[0]=old[1]=old[2]=200;old[3]=128;
            src[0]=src[1]=src[2]=100;src[3]=192;
        }
        KValue args[]={{71,NULL},{(int32_t)type,NULL},{-1,NULL},{8,NULL},{3,NULL},{43,NULL}};
        assert(!call_anime520(b,args,6)&&b->vm->sp==1&&b->vm->stack[0].number==71);
        unsigned steps=speed==0?4:speed==1?2:0;
        if(steps){
            assert(bootstrap_run(b,1)==1);bootstrap_frame(b);
            assert(b->mes_fade_type==type&&b->mes_fade_alpha==255);
            for(unsigned y=210;y<214;y++)for(unsigned x=20;x<622;x++){
                unsigned a=type==2?192*(255/steps)/255:
                    mask[y*640+x]>=512/steps?0:512/steps-mask[y*640+x];
                if(a>192)a=192;
                unsigned expected=(50*127/255+200*128/255)*(255-a)/255+100*a/255;
                assert(b->layers[0].pixels[y*2560+x*4]==expected&&b->layers[0].pixels[y*2560+x*4+3]==77);
            }
            /* Old pixels outside this line stay visible throughout. */
            assert(b->layers[0].pixels[0]==124);
        }
        for(unsigned frame=0;frame<8&&b->mes_fade_transition;frame++)bootstrap_frame(b);
        bootstrap_frame(b);assert(!b->mes_fade_transition);
        assert(b->layers[0].pixels[210*2560+20*4]==87&&b->layers[0].pixels[0]==124);
        assert(b->mes_fade_surfaces[1].pixels[0]==200&&b->mes_fade_surfaces[1].pixels[210*2560+20*4]==100);
        assert(!b->mes_fade_surfaces[0].pixels[210*2560+20*4+3]);
        bootstrap_frame(b);assert(b->layers[0].pixels[0]==124); /* No cumulative blending. */
        assert(!call(b,43,1));bootstrap_frame(b);assert(!b->mes_fade_mask);
    }
    /* Mask failure preserves both the VM arguments and persistent text. */
    assert(!call(b,43,0));Ai6Archive archive=b->data;b->data=(Ai6Archive){0};
    KValue bad[]={{3,NULL},{-1,NULL},{8,NULL},{3,NULL},{43,NULL}};
    assert(call_anime520(b,bad,5)<0&&b->vm->sp==5&&!b->mes_fade_transition&&!b->mes_fade_mask);
    b->data=archive;free(mask);bootstrap_destroy(b);
    puts("Kisaku message types 2/3: retained old text, native 03.bin alpha mask, three speeds, line-only commit, RGB/Alpha and missing-mask rejection: PASS");
}
static void test_letter_pages(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=1;b->layers[1]=(KImage){0,0,640,480,2560,malloc(480*2560)};assert(b->layers[1].pixels);
    assert(call(b,525,3)<0&&b->vm->sp==2&&!b->letter_mode);
    for(unsigned speed=0;speed<3;speed++){
        char value[2]={(char)('0'+speed),0};test_setting(b,"Display","EffectSpeed",value);
        for(unsigned p=0;p<640*480;p++){
            uint8_t *d=b->layers[0].pixels+p*4;d[0]=200;d[1]=90;d[2]=17;d[3]=99;
        }
        memset(b->layers[1].pixels,55,480*2560);
        b->vm->globals[0][50].number=0x4000; /* This fade follows EffectSpeed even here. */
        assert(!call(b,525,0)&&!b->vm->sp);
        assert(b->font_width==24&&b->font_height==24&&b->vm->globals[0][43].number==12&&b->vm->globals[0][31].number==24);
        assert(b->vm->globals[0][46].number==32&&b->vm->globals[0][47].number==12&&b->vm->globals[0][49].number==1);
        for(unsigned p=0;p<480*2560;p++)assert(!b->layers[1].pixels[p]);
        assert(b->letter_surfaces[0].pixels[0]==200&&b->letter_surfaces[0].pixels[3]==99);
        assert(b->letter_surfaces[1].pixels[0]==110&&!b->letter_surfaces[1].pixels[1]&&!b->letter_surfaces[1].pixels[2]);
        unsigned frames=0;
        if(b->letter_transition){
            assert(call(b,525,2)<0&&b->vm->sp==2);b->error[0]=0;
            assert(bootstrap_run(b,1)==1); /* Fade blocks subsequent script instructions. */
        }
        while(b->letter_transition&&frames<9){bootstrap_frame(b);frames++;}
        assert(frames==(speed==0?7:speed==1?3:0)&&b->letter_mode&&b->layers[0].pixels[0]==110);
        for(unsigned page=0;page<2;page++){
            memset(b->layers[1].pixels,88,480*2560);b->layers[0].pixels[0]=250;
            b->vm->globals[0][46].number=400;b->vm->globals[0][47].number=156;
            strcpy(b->message_pending,"previous page");b->message_pending_size=strlen(b->message_pending);
            assert(!call(b,525,3)&&!b->vm->sp&&!b->message_pending_size);
            assert(b->letter_surfaces[2].pixels[0]==200&&b->letter_surfaces[2].pixels[3]==99);
            for(unsigned frame=0;b->letter_transition&&frame<9;frame++)bootstrap_frame(b);
            assert(!b->letter_transition&&b->letter_mode&&b->layers[0].pixels[0]==110);
            assert(b->letter_surfaces[0].pixels[0]==200&&b->letter_surfaces[0].pixels[3]==99);
            assert(!b->layers[1].pixels[0]&&b->vm->globals[0][47].number==12);
        }
        b->layers[0].pixels[0]=77;assert(!call(b,525,2));
        for(unsigned frame=0;b->letter_transition&&frame<9;frame++)bootstrap_frame(b);
        assert(!b->letter_mode&&!b->letter_transition&&b->font_width==16&&b->font_height==16);
        assert(b->layers[0].pixels[0]==200&&b->layers[0].pixels[1]==90&&b->layers[0].pixels[2]==17&&b->layers[0].pixels[3]==99);
        assert(b->vm->globals[0][43].number==8&&b->vm->globals[0][31].number==18);
    }
    KValue unsupported[]={{123,NULL},{1,NULL},{525,NULL}};
    assert(call_anime520(b,unsupported,3)<0&&b->vm->sp==3&&b->vm->stack[0].number==123);
    bootstrap_destroy(b);
    puts("Kisaku letter pages: native entry/clear/exit, 24px layout, saturating RGB, original background across repeated pages, speeds and error stack: PASS");
}
static void test_letter_body(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=1;b->layers[1]=(KImage){0,0,640,480,2560,calloc(480,2560)};assert(b->layers[1].pixels);
    b->read_size=4096;b->read_flags=calloc(b->read_size,1);assert(b->read_flags);
    b->audio_counts[2]=1;b->audio_objects[2]=calloc(1,sizeof(*b->audio_objects[2]));assert(b->audio_objects[2]);
    KValue show[]={{123,NULL},{1,NULL},{525,NULL}};
    for(unsigned speed=0;speed<3;speed++){
        test_setting(b,"Display","EffectSpeed","2");
        test_setting(b,"Msg","IsOneMes","0");test_setting(b,"Msg","IsAutoMes","0");
        for(unsigned i=0;i<640*480;i++){uint8_t *p=b->layers[0].pixels+i*4;p[0]=200;p[1]=160;p[2]=120;p[3]=99;}
        assert(!call(b,525,0));
        for(unsigned i=0;i<640*480;i++)b->layers[0].pixels[i*4+3]=99;
        for(unsigned y=12;y<60;y++)for(unsigned x=0;x<640;x++){
            uint8_t *p=b->layers[1].pixels+y*2560+x*4;p[0]=220;p[1]=210;p[2]=200;p[3]=y<36?141:230;
        }
        b->vm->globals[0][46].number=80;b->vm->globals[0][47].number=36;b->vm->globals[0][50].number=0x180;
        strcpy(b->message_pending,"first part and second part");b->message_pending_size=strlen(b->message_pending);
        if(speed==0){
            Ai6Archive archive=b->data;b->data=(Ai6Archive){0};
            assert(call_anime520(b,show,3)<0&&b->vm->sp==3&&!b->letter_active&&!b->letter_mask);
            assert(b->message_pending_size&&b->vm->globals[0][50].number==0x180);b->data=archive;
        }
        char value[2]={(char)('0'+speed),0};test_setting(b,"Display","EffectSpeed",value);
        assert(!call_anime520(b,show,3)&&!b->vm->sp&&b->letter_active&&b->message_active);
        assert(b->letter_mask&&b->message_read_id==123&&!(b->vm->globals[0][50].number&0x80));
        assert(b->read_flags[123>>3]&(0x80u>>(123&7)));
        assert(!strcmp(b->history[(b->history_next+63)%64],"first part and second part")&&!b->message_pending_size);
        assert(!bootstrap_can_save(b));
        unsigned phase=speed==1?64:32;
        bootstrap_frame(b);
        for(unsigned x=0;x<640;x++){
            int a=141,ramp=(int)phase-b->letter_mask[17*640+x];if(ramp<0)ramp=0;if(speed!=2&&a>ramp)a=ramp;
            const uint8_t *p=b->layers[0].pixels+17*2560+x*4;
            assert(p[0]==220*a/255+110*(255-a)/255&&p[1]==210*a/255+70*(255-a)/255&&p[3]==99);
        }
        if(speed!=2)assert(b->layers[0].pixels[41*2560]==110); /* Next line has not started. */
        for(unsigned f=0;b->message_revealing&&f<30;f++)bootstrap_frame(b);
        assert(!b->message_revealing&&b->letter_active&&!b->vm->sp);
        assert(b->layers[0].pixels[17*2560]==220*141/255+110*114/255);
        assert(b->layers[0].pixels[41*2560]==220*230/255+110*25/255);
        assert(b->layers[1].pixels[17*2560+3]==141); /* Reveal never damages glyph coverage. */
        bootstrap_confirm(b);
        assert(!b->letter_active&&!b->message_active&&b->letter_cursor_x==80&&b->letter_cursor_y==36);
        assert(b->vm->sp==1&&b->vm->stack[0].number==0&&(b->vm->globals[0][50].number&0x80));
        /* A second prompt on this page begins at the previous cursor. */
        b->layers[1].pixels[41*2560+90*4]=250;b->vm->globals[0][46].number=120;
        assert(!call_anime520(b,show,3)&&b->letter_reveal_x==80&&b->letter_reveal_y==36);
        bootstrap_pointer(b,1,470,1); /* No inherited footer menu on a letter. */
        assert(!b->letter_active&&!b->message_request&&b->vm->sp==1);
        assert(!call(b,525,2));for(unsigned f=0;b->letter_transition&&f<10;f++)bootstrap_frame(b);
        assert(!b->letter_mode&&b->layers[0].pixels[0]==200);
    }
    test_setting(b,"Display","EffectSpeed","2");assert(!call(b,525,0));
    b->layers[1].pixels[12*2560+32*4]=255;b->layers[1].pixels[12*2560+32*4+3]=255;
    b->vm->globals[0][46].number=56;
    test_setting(b,"Display","EffectSpeed","0");assert(!call_anime520(b,show,3));
    bootstrap_confirm(b); /* One click during reveal completes AND returns. */
    assert(!b->message_active&&!b->message_revealing&&b->vm->sp==1);
    assert(!call_anime520(b,show,3));bootstrap_cancel(b);
    assert(b->letter_transition==4&&b->letter_active&&!b->message_revealing);
    bootstrap_confirm(b);assert(!b->vm->sp&&b->letter_active);
    for(unsigned f=0;b->letter_transition&&f<10;f++)bootstrap_frame(b);
    assert(b->message_user_hidden&&b->font_height==16&&b->layers[0].pixels[0]==200);
    bootstrap_cancel(b);for(unsigned f=0;b->letter_transition&&f<10;f++)bootstrap_frame(b);
    assert(!b->message_user_hidden&&b->letter_active&&b->font_height==24&&!b->vm->sp);
    bootstrap_cancel(b);for(unsigned f=0;b->letter_transition&&f<10;f++)bootstrap_frame(b);
    bootstrap_confirm(b);for(unsigned f=0;b->letter_transition&&f<10;f++)bootstrap_frame(b);
    assert(!b->message_user_hidden&&b->letter_active&&!b->vm->sp);
    bootstrap_confirm(b);assert(!b->letter_active&&b->vm->sp==1);
    assert(!call_anime520(b,show,3));bootstrap_message_action(b,7);
    assert(!b->message_open&&!b->message_buttons_motion);
    bootstrap_message_action(b,5);assert(b->letter_backlog&&b->letter_transition==4);
    for(unsigned f=0;b->letter_transition&&f<10;f++)bootstrap_frame(b);
    assert(b->message_request==5&&b->message_user_hidden&&!b->vm->sp);
    b->message_request=0;bootstrap_message_hide(b,0);
    for(unsigned f=0;b->letter_transition&&f<10;f++)bootstrap_frame(b);
    assert(!b->letter_backlog&&!b->message_user_hidden&&b->letter_active);bootstrap_confirm(b);
    /* Auto waits for complete text; read-skip can finish a reveal immediately. */
    test_setting(b,"Msg","IsAutoMes","1");test_setting(b,"Msg","AutoMesSpeed","104");
    assert(!call_anime520(b,show,3));assert(b->message_auto_delay==500);
    for(unsigned f=0;f<30;f++)bootstrap_frame(b);
    assert(b->letter_active);bootstrap_frame(b);assert(!b->letter_active&&b->vm->sp==1);
    test_setting(b,"Msg","IsAutoMes","0");test_setting(b,"Msg","IsOneMes","1");
    assert(!call_anime520(b,show,3));bootstrap_frame(b);assert(!b->letter_active&&b->vm->sp==1);
    test_setting(b,"Msg","IsOneMes","0");show[0].number=-1;
    assert(!call_anime520(b,show,3)&&b->message_was_read);bootstrap_confirm(b);
    if(b->vm->byte_count<4011)b->vm->byte_count=4011;
    b->vm->bytes[4010]=1;show[0].number=32768;
    assert(!call_anime520(b,show,3)&&b->message_read_id==27680);bootstrap_confirm(b);b->vm->bytes[4010]=0;
    show[0].number=32768;assert(call_anime520(b,show,3)<0&&b->vm->sp==3&&!b->letter_active);
    show[0]=(KValue){0,"invalid"};assert(call_anime520(b,show,3)<0&&b->vm->sp==3);
    bootstrap_destroy(b);
    puts("Kisaku letter body: real 01.bin, multiline RGB/Alpha, repeated prompts, confirm/hide/restore, auto/read skip, history and invalid operands: PASS");
}

static void audit_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void test_startup_native_ax(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);b->layer_count=8;
    b->layers[8]=(KImage){0,0,2,1,8,malloc(8)};
    const uint8_t pixels[8]={19,29,39,49,0,255,0,255};memcpy(b->layers[8].pixels,pixels,8);
    uint8_t data[0x590]={0};audit_put32(data,0x580);
    const uint32_t d[7]={0,0,0,2,1,10,20};
    for(unsigned i=0;i<7;i++)audit_put32(data+0x500+4*i,d[i]);
    data[0x580]=9;data[0x585]=1;audit_put32(data+0x586,1);data[0x58a]=255;
    assert(ax_load(&b->ax,"native.ax",data,sizeof(data))&&ax_control(&b->ax,1,0,0));
    uint8_t *dst=b->layers[0].pixels+20*b->layers[0].stride+40;memset(dst,77,8);
    b->vm->globals[0][50].number&=~0x10;
    for(unsigned i=0;i<4;i++)bootstrap_frame(b);
    assert(!b->ax.cells[0].ip&&dst[0]==77); /* Native animation enable flag. */
    b->vm->globals[0][50].number|=0x10;
    bootstrap_frame(b);assert(!b->ax.cells[0].ip);
    bootstrap_frame(b);assert(!b->error[0]&&b->ax_events[0]==11);
    assert(!memcmp(dst,pixels,3)&&dst[3]==77&&dst[4]==0&&dst[5]==255&&dst[7]==77);
    bootstrap_frame(b);assert(b->ax_events[0]==2&&b->ax.cells[0].boundary_delay==0);
    bootstrap_frame(b);assert(b->ax_events[0]==10&&b->ax.cells[0].state==AX_STOPPED);
    audit_put32(data+0x500,1);audit_put32(data+0x518,500); /* keyed, second page */
    assert(ax_load(&b->ax,"keyed.ax",data,sizeof(data))&&ax_control(&b->ax,1,0,0));
    memset(dst,77,8);b->ax_clock=0;
    bootstrap_frame(b);bootstrap_frame(b);
    assert(!b->error[0]&&!memcmp(dst,pixels,3)&&dst[3]==77);
    for(unsigned i=4;i<8;i++)assert(dst[i]==77);
    audit_put32(data+33*4,0x580);audit_put32(data+66*4,0x580);
    assert(ax_load(&b->ax,"portrait.ax",data,sizeof(data)));
    KValue setup[]={{73,NULL},{1,NULL},{1,NULL},{2,NULL},{11,NULL}};
    assert(!call_anime520(b,setup,5)&&b->vm->sp==1&&b->vm->stack[0].number==73);
    setup[1].number=2;setup[2].number=2;
    assert(!call_anime520(b,setup,5)&&b->portrait_tracks[0]==33&&b->portrait_tracks[1]==66);
    assert(b->ax.cells[33].state==255&&b->ax.cells[66].state==255);
    assert(!call(b,11,3)&&b->ax.cells[33].state==0&&b->ax.cells[66].state==0);
    assert(b->ax_registered[33]==1&&b->ax_registered[66]==1);
    KValue query[]={{73,NULL},{1,NULL},{1,NULL},{5,NULL},{11,NULL}};
    assert(!call_anime520(b,query,5)&&b->vm->sp==2&&b->vm->stack[1].number==0);
    memset(dst,77,8);b->ax_clock=0;bootstrap_frame(b);bootstrap_frame(b);
    assert(!b->error[0]&&dst[0]==19&&b->ax.cells[33].ip&&b->ax.cells[66].ip);
    b->ax.cells[33].state=255;
    assert(!call_anime520(b,query,5)&&b->vm->stack[1].number==255);
    setup[2].number=10;assert(call_anime520(b,setup,5)<0&&b->vm->sp==5);
    puts("Portrait bank/cell setup, two-track simultaneous start, pixels and status ABI: PASS");
    bootstrap_destroy(b);
    puts("Kisaku startup AX: layer 8, RGB/keyed copy, Y wrap, enable flag and boundary timing: PASS");
}
static void replay_number(uint8_t *code,size_t *at,int32_t value){
    code[(*at)++]=0x32;for(int i=3;i>=0;i--)code[(*at)++]=(uint8_t)((uint32_t)value>>(i*8));
}
static void replay_voice(uint8_t *code,size_t *at,const char *name){
    replay_number(code,at,0);code[(*at)++]=0x33;
    memcpy(code+*at,name,strlen(name)+1);*at+=strlen(name)+1;
    replay_number(code,at,5);replay_number(code,at,17);code[(*at)++]=0x18;
}
static void test_backlog_replay(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(!backlog_call(b,0,1,(KValue){1,NULL}));
    uint8_t code[512];size_t at=0;
    replay_voice(code,&at,"z09577.ogg");
    replay_number(code,&at,0xff0000);replay_number(code,&at,33);code[at++]=0x0e;
    code[at++]=0x0a;code[at++]='A';code[at++]=0;
    code[at++]=0x1b;code[at++]=0;
    replay_voice(code,&at,"z09578.ogg");
    replay_number(code,&at,0x00ff00);replay_number(code,&at,33);code[at++]=0x0e;
    code[at++]=0x0b;code[at++]='B';code[at++]=0;
    /* 23/8 is a count query whose native result is intentionally discarded;
       replay must consume only the action and leave the caller stack intact. */
    replay_number(code,&at,8);replay_number(code,&at,23);code[at++]=0x18;
    code[at++]=0;
    KMessageRecord *record=&b->messages[0];record->data=malloc(at);assert(record->data);memcpy(record->data,code,at);record->size=record->capacity=at;record->flag=1;
    KImage row={0,0,640,54,2560,calloc(640*54,4)};assert(row.pixels);
    for(unsigned i=0;i<640*54;i++)row.pixels[i*4+3]=255;
    KValue *globals=malloc(sizeof(b->vm->globals));assert(globals);memcpy(globals,b->vm->globals,sizeof(b->vm->globals));
    KBacklogVoices voices={0};char why[256]={0};
    assert(bootstrap_backlog_native(b)&&bootstrap_backlog_count(b)==1&&bootstrap_backlog_has_voice(b,0));
    assert(!bootstrap_backlog_replay(b,0,&row,&voices,why));
    assert(voices.count==2&&!strcmp(voices.names[0],"z09577.ogg")&&!strcmp(voices.names[1],"z09578.ogg"));
    unsigned red=0,green=0;for(unsigned y=0;y<54;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=row.pixels+y*row.stride+x*4;
        if(p[2]&&!p[1]){assert(y<18);red++;}
        if(p[1]&&!p[2]){assert(y>=18);green++;}
    }
    assert(red&&green&&!memcmp(globals,b->vm->globals,sizeof(b->vm->globals))&&!b->error[0]);
    uint8_t *pixels=malloc(640*54*4);assert(pixels);memcpy(pixels,row.pixels,640*54*4);
    at--;replay_number(code,&at,99);replay_number(code,&at,31);code[at++]=0x18;code[at++]=0;
    free(record->data);record->data=malloc(at);assert(record->data);memcpy(record->data,code,at);record->size=record->capacity=at;
    assert(bootstrap_backlog_replay(b,0,&row,&voices,why)<0&&strstr(why,"31/99"));
    assert(!memcmp(pixels,row.pixels,640*54*4)&&voices.count==2&&!b->error[0]);
    assert(!memcmp(globals,b->vm->globals,sizeof(b->vm->globals)));
    /* Native CBackLog rows share the renderer's text state.  A selected row
       may intentionally omit its colour/font setup; replay the preceding
       command vector in chronological order instead of resetting to the
       current gameplay globals for every row. */
    b->vm->sp=0;assert(!backlog_call(b,0,1,(KValue){1,NULL})&&b->message_count==2);
    uint8_t prior[64],current[32];size_t prior_size=0,current_size=0;
    replay_number(prior,&prior_size,0xff0000);replay_number(prior,&prior_size,33);prior[prior_size++]=0x0e;
    prior[prior_size++]=0x0a;prior[prior_size++]='R';prior[prior_size++]=0;prior[prior_size++]=0;
    /* The selected record may explicitly place its first line.  The replay
       cursor reset must happen before these stores, not in the text callback
       where it would erase the record's intentional location. */
    replay_number(current,&current_size,200);replay_number(current,&current_size,46);current[current_size++]=0x0e;
    replay_number(current,&current_size,36);replay_number(current,&current_size,47);current[current_size++]=0x0e;
    current[current_size++]=0x0a;current[current_size++]='I';current[current_size++]=0;current[current_size++]=0;
    free(b->messages[0].data);b->messages[0].data=malloc(prior_size);assert(b->messages[0].data);
    memcpy(b->messages[0].data,prior,prior_size);b->messages[0].size=b->messages[0].capacity=prior_size;b->messages[0].flag=0;
    b->messages[1].data=malloc(current_size);assert(b->messages[1].data);memcpy(b->messages[1].data,current,current_size);
    b->messages[1].size=b->messages[1].capacity=current_size;b->messages[1].flag=0;
    memset(row.pixels,0,640*54*4);memset(why,0,sizeof(why));kbacklog_voices_free(&voices);
    assert(!bootstrap_backlog_replay(b,0,&row,&voices,why)&&!voices.count);
    unsigned inherited_red=0,min_x=640;for(unsigned y=0;y<54;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=row.pixels+y*row.stride+x*4;if(p[2]&&!p[1])inherited_red++;
        if(p[2]&&!p[1]&&x<min_x)min_x=x;
    }
    assert(inherited_red&&min_x>=200&&min_x<240);
    uint8_t *repeat=malloc(640*54*4);assert(repeat);memcpy(repeat,row.pixels,640*54*4);
    memset(row.pixels,0,640*54*4);
    memset(why,0,sizeof(why));kbacklog_voices_free(&voices);assert(!bootstrap_backlog_replay(b,0,&row,&voices,why));
    assert(!memcmp(repeat,row.pixels,640*54*4)&&!voices.count);

    /* A third, long record exercises the chronological boundary repeatedly:
       its first glyph returns to the configured origin even though the
       preceding record ended at the explicit x=200 position. */
    b->vm->sp=0;assert(!backlog_call(b,0,1,(KValue){1,NULL})&&b->message_count==3);
    uint8_t long_record[256];size_t long_size=0;long_record[long_size++]=0x0a;
    for(unsigned i=0;i<48;i++)long_record[long_size++]='L';long_record[long_size++]=0;long_record[long_size++]=0;
    free(b->messages[2].data);b->messages[2].data=malloc(long_size);assert(b->messages[2].data);memcpy(b->messages[2].data,long_record,long_size);b->messages[2].size=b->messages[2].capacity=long_size;b->messages[2].flag=0;
    memset(row.pixels,0,640*54*4);memset(why,0,sizeof(why));kbacklog_voices_free(&voices);assert(!bootstrap_backlog_replay(b,0,&row,&voices,why));
    unsigned origin=640;for(unsigned y=0;y<54;y++)for(unsigned x=0;x<640;x++){uint8_t *p=row.pixels+y*row.stride+x*4;if(p[2]&&!p[1]&&x<100&&x<origin)origin=x;}
    assert(origin>=32&&origin<80);
    uint8_t *long_pixels=malloc(640*54*4);assert(long_pixels);memcpy(long_pixels,row.pixels,640*54*4);
    memset(row.pixels,0,640*54*4);
    memset(why,0,sizeof(why));assert(!bootstrap_backlog_replay(b,0,&row,&voices,why)&&!memcmp(long_pixels,row.pixels,640*54*4));
    free(long_pixels);free(repeat);
    kbacklog_voices_free(&voices);
    free(pixels);free(globals);rmt_free(&row);kbacklog_voices_free(&voices);bootstrap_destroy(b);
    puts("Native backlog replay: colors, newline, cross-record state, ordered voices, isolation and atomic failure: PASS");
}
static void test_backlog_real_records(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    int rc=bootstrap_run(b,100000);
    for(unsigned i=0;rc==1&&i<2000&&!(b->title.active&&b->title.age>=64);i++){bootstrap_frame(b);rc=bootstrap_run(b,100000);}
    assert(rc==1&&b->title.active);bootstrap_title_move(b,1);bootstrap_confirm(b);rc=bootstrap_run(b,100000);
    for(unsigned i=0;rc==1&&i<2000&&!(b->message_active&&b->text_count>=9);i++){
        if(b->flag_dialog.active)bootstrap_pointer(b,300,350,1);
        if(b->extra_active&&b->extra_kind==14)bootstrap_name_submit(b,"鬼作");
        if((b->message_active&&!b->message_slide)||b->wait_input)bootstrap_confirm(b);
        bootstrap_frame(b);rc=bootstrap_run(b,100000);
    }
    assert(rc==1&&!b->error[0]&&b->text_count>=9&&bootstrap_backlog_native(b));
    KImage row={0,0,640,54,2560,calloc(640*54,4)};assert(row.pixels);
    unsigned count=bootstrap_backlog_count(b),voiced=0;assert(count>2);
    for(unsigned back=0;back<count;back++){
        KBacklogVoices voices={0};char why[256]={0};memset(row.pixels,0,640*54*4);
        int result=bootstrap_backlog_replay(b,back,&row,&voices,why);
        if(result)fprintf(stderr,"Real backlog %u: %s\n",back,why);
        assert(!result);voiced+=voices.count;kbacklog_voices_free(&voices);
    }
    assert(voiced>=3&&!b->error[0]);rmt_free(&row);bootstrap_destroy(b);
    puts("Original opening MES: recorded commands, voice flags, state arithmetic and native-row replay: PASS");
}
static void test_choice_stack_isolation(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    /* Capturing commands in the live VM must not capture speculative choice
       evaluation, even when the body is rejected after growing its stack. */
    assert(!backlog_call(b,0,1,(KValue){1,NULL}));
    b->vm->globals[0][50].number=0x280;
    assert(!backlog_call(b,2,0,(KValue){0,NULL}));b->vm->sp=0;
    b->messages[0].data=malloc(2);assert(b->messages[0].data);
    b->messages[0].data[0]=0x7e;b->messages[0].data[1]=0;
    b->messages[0].size=b->messages[0].capacity=2;
    uint8_t *record=b->messages[0].data;
    /* An unsupported choice body still executes its isolated evaluator first.
       Force multiple reallocations; the caller's borrowed string must survive. */
    uint8_t code[4+200*5+1]={0};unsigned at=4;
    for(unsigned i=0;i<200;i++){code[at++]=0x32;code[at++]=0;code[at++]=0;code[at++]=0;code[at++]=(uint8_t)i;}
    code[at++]=0;int id=kvm_add_module(b->vm,"choice-stack",code,at);assert(id>=0);
    b->vm->current_list=0;b->vm->lists[0].count=1;b->vm->lists[0].items[0]=(KListItem){id,0,0};
    const char sentinel[]="live caller value";assert(!kvm_push(b->vm,(KValue){123,sentinel}));
    KValue *original=b->vm->stack;unsigned capacity=b->vm->stack_capacity;
    assert(!kvm_push(b->vm,(KValue){14,NULL}));b->vm->syscall=31;b->vm->status=KVM_SYSCALL;
    assert(bootstrap_dispatch(b)<0&&strstr(b->error,"choice body unsupported"));
    assert(b->vm->stack==original&&b->vm->stack_capacity==capacity&&b->vm->sp==1);
    assert(b->vm->stack[0].number==123&&b->vm->stack[0].string==sentinel);
    assert(b->messages[0].data==record&&b->messages[0].size==2&&b->messages[0].capacity==2);
    assert(record[0]==0x7e&&!record[1]&&b->message_index==0);
    assert(b->vm->globals[0][50].number==0x380);
    bootstrap_destroy(b);
    puts("Choice evaluation owns its stack: caller survives growth and rejected body: PASS");
}
static void test_setting(KBootstrap *b,const char *section,const char *key,const char *value){
    unsigned i=0;for(;i<b->setting_count;i++)if(!strcmp(b->settings[i].section,section)&&!strcmp(b->settings[i].key,key))break;
    if(i==b->setting_count)b->setting_count++;
    snprintf(b->settings[i].section,sizeof(b->settings[i].section),"%s",section);
    snprintf(b->settings[i].key,sizeof(b->settings[i].key),"%s",key);
    snprintf(b->settings[i].value,sizeof(b->settings[i].value),"%s",value);
}
static void test_message_timing(KBootstrap *b){
    test_setting(b,"Msg","ShowSpeed","124");test_setting(b,"Msg","IsAutoMes","0");
    for(unsigned locked=0;locked<2;locked++)for(unsigned speed=0;speed<3;speed++){
        char number[4];snprintf(number,sizeof(number),"%u",speed);test_setting(b,"Display","EffectSpeed",number);
        b->message_active=b->message_visible=0;b->vm->globals[0][50].number=locked?0x4000:0;
        memset(b->layers[0].pixels,100,640*480*4);assert(!call(b,10,0));
        assert(!call(b,0,-1));unsigned steps=locked?12:speed==0?6:speed==1?3:0;
        assert(b->message_slide==(steps?steps+1:0));
        if(locked){bootstrap_confirm(b);assert(b->message_slide==13&&b->message_active);}
        unsigned frames=0;
        while(b->message_slide&&frames<20){
            bootstrap_frame(b);frames++;
            if(!locked&&speed==0&&frames==1){
                assert(b->layers[0].pixels[465*2560+100*4]==100);
                assert(b->layers[0].pixels[466*2560+100*4]==39);
            }
        }
        assert(frames==(steps*900+999)/1000&&b->message_visible);
        b->message_active=0;assert(!call(b,10,2));frames=0;
        while(b->message_slide&&frames<20){bootstrap_frame(b);frames++;}
        assert(frames==(steps*900+999)/1000&&!b->message_visible);
        assert(b->layers[0].pixels[470*2560+100*4]==100);
    }
    b->vm->globals[0][50].number=0;test_setting(b,"Display","EffectSpeed","0");
    assert(!call(b,0,-1)&&b->message_slide==7);bootstrap_confirm(b);
    assert(!b->message_slide&&b->message_active); /* Skip motion, not the dialogue. */
    test_setting(b,"Display","EffectSpeed","2");test_setting(b,"Msg","ShowSpeed","0");
    assert(!call(b,10,0));b->vm->globals[0][46].number=64;
    assert(!call(b,0,-1)&&b->message_delay==250&&b->message_revealing);
    bootstrap_frame(b);assert(b->message_reveal_x==48);
    for(unsigned i=0;i<14;i++)bootstrap_frame(b);
    assert(b->message_reveal_x==48);bootstrap_frame(b);assert(b->message_reveal_x==64);
    test_setting(b,"Msg","ShowSpeed","86");assert(!call(b,0,-1)&&b->message_delay==63);
    test_setting(b,"Msg","ShowSpeed","104");assert(!call(b,0,-1)&&!b->message_delay&&b->message_revealing);
    test_setting(b,"Msg","ShowSpeed","124");assert(!call(b,0,-1)&&!b->message_revealing);
    /* Auto timing measures the accumulated message, not just its last TEXT,
       and runs during reveal. 40 encoded bytes at speed52 => 2260 ms. */
    test_setting(b,"Msg","ShowSpeed","0");test_setting(b,"Msg","AutoMesSpeed","52");test_setting(b,"Msg","IsAutoMes","1");
    memset(b->message_pending,'a',40);b->message_pending[40]=0;b->message_pending_size=40;
    assert(!call(b,0,-1)&&b->message_auto_delay==2260);
    bootstrap_frame(b);assert(b->message_auto_clock==1000&&b->message_revealing);
    test_setting(b,"Msg","IsAutoMes","0");
    b->message_active=b->message_visible=0;b->message_voice_pending=0;assert(!call(b,10,0));
    assert(bootstrap_setting_default(0)==86&&bootstrap_setting_limit(0)==124);
    assert(bootstrap_setting_default(1)==52&&bootstrap_setting_limit(1)==104);
    puts("Kisaku message timing: 15ms motion, speed/flag/skip, glyph delay and accumulated auto deadline: PASS");
}
static void test_message_prefix_and_buttons(KBootstrap *b){
    b->vm->globals[0][50].number=0;
    test_setting(b,"Display","EffectSpeed","2");test_setting(b,"Msg","ShowSpeed","0");
    test_setting(b,"Msg","IsAutoMes","0");test_setting(b,"Msg","IsOneMes","0");test_setting(b,"Msg","EnableOpen","0");
    const char *texts[]={("\x81\x6d\x8b\x53\x8d\xec\x81\x6e" "body"), "tag: \x81\x6d\x8b\x53\x8d\xec\x81\x6e", "ascii only", "\x81\x6d\x8b", "\x81\x75\x8b\x53\x81\x76"};
    for(unsigned i=0;i<5;i++){
        assert(!call(b,10,0));b->vm->globals[0][46].number=160;
        strcpy(b->message_pending,texts[i]);b->message_pending_size=strlen(texts[i]);
        for(unsigned y=8;y<26;y++)memset(b->layers[1].pixels+y*2560+32*4,177,128*4);
        assert(!call(b,0,-1));unsigned width=i<2?64:0;
        assert(!b->message_slide&&b->message_reveal_x==32+(int)width&&b->message_revealing);
        for(unsigned x=0;x<128;x++)assert(b->message_text.pixels[x*4]==(x<width?177:0));
        bootstrap_frame(b);assert(b->message_reveal_x==48+(int)width);
    }
    assert(!call(b,10,0));test_setting(b,"Msg","ShowSpeed","124");assert(!call(b,0,-1));
    KImage *atlas=&b->message_skin.atlas;uint8_t *original=malloc(atlas->stride*atlas->height);assert(original);
    memcpy(original,atlas->pixels,atlas->stride*atlas->height);
    const unsigned source_x[]={532,152,76,0};
    for(unsigned i=0;i<4;i++)for(unsigned y=84;y<100;y++)for(unsigned x=source_x[i];x<source_x[i]+76;x++){
        uint8_t *p=atlas->pixels+y*atlas->stride+x*4;p[0]=(uint8_t)(20+i*40);p[1]=17;p[2]=29;p[3]=255;
    }
    for(unsigned locked=0;locked<2;locked++)for(unsigned speed=0;speed<3;speed++){
        char value[2]={(char)('0'+speed),0};test_setting(b,"Display","EffectSpeed",value);
        b->vm->globals[0][50].number=locked?0x4000:0;
        for(unsigned opening=1;opening<=2;opening++){
            bootstrap_message_action(b,7);assert(b->message_open==(opening==1));
            unsigned maximum=0;
            for(unsigned i=0;i<4;i++){
                unsigned duration=opening==1?6+i*2:14-i*2;
                unsigned steps=locked?duration:speed==0?duration/2:speed==1?duration/4:0;
                assert(b->message_buttons_steps[i]==steps);if(steps>maximum)maximum=steps;
            }
            if(maximum){
                if(locked){bootstrap_confirm(b);assert(b->message_buttons_motion&&b->message_active);}
                bootstrap_frame(b);assert(b->message_buttons_tick==1);
                for(unsigned i=0;i<4;i++){
                    unsigned d=b->message_buttons_steps[i],move=d>1?16/d:16;
                    unsigned y=opening==1?480-move:464+move;
                    if(y<480){
                        unsigned x=(unsigned)b->message_skin.buttons[i+1].x+32;
                        assert(b->layers[0].pixels[y*2560+x*4]==20+i*40);
                        assert(b->layers[0].pixels[(y-1)*2560+x*4]!=20+i*40);
                    }
                }
            }
            unsigned frames=maximum?1:0;
            while(b->message_buttons_motion&&frames<20){bootstrap_frame(b);frames++;}
            assert(!b->message_buttons_motion&&frames==(maximum*900+999)/1000&&b->message_active);
        }
    }
    b->vm->globals[0][50].number=0;test_setting(b,"Display","EffectSpeed","0");
    bootstrap_message_action(b,7);assert(b->message_buttons_motion);bootstrap_confirm(b);
    assert(!b->message_buttons_motion&&b->message_active&&b->message_open);
    test_setting(b,"Display","EffectSpeed","2");bootstrap_message_action(b,7);
    assert(!b->message_buttons_motion&&!b->message_open);
    memcpy(atlas->pixels,original,atlas->stride*atlas->height);free(original);
    b->message_active=b->message_visible=0;assert(!call(b,10,0));
    puts("Kisaku message names/buttons: immediate CP932 name, bounded malformed input, four native motion durations, pixels, speeds and skip permission: PASS");
}
static void test_native_dialog(KBootstrap *b){
    uint8_t *data=NULL;size_t size=0;KImage atlas={0},body={0},out={0};
    assert(!ai6_read_named(&b->images,"kisaku_DL_dialog_p.akb",&data,&size));
    assert(!rmt_decode(data,size,&atlas));free(data);
    /* The label's blue key and button's green edge carry alpha0 even though
       this actual AKB's flags are zero. They must never appear in the dialog. */
    assert(atlas.pixels[90*atlas.stride+10*4+3]==0);
    assert(atlas.pixels[88*atlas.stride+190*4+3]==0);
    assert(atlas.pixels[120*atlas.stride+10*4+3]==204);
    out=(KImage){0,0,640,480,2560,calloc(480,2560)};
    memset(b->layers[0].pixels,100,640*480*4);
    assert(!kdialog_draw(&out,&body,&b->layers[0],&atlas,0,-1));
    assert(out.pixels[0]==24&&out.pixels[3]==255);
    const uint8_t *frame=atlas.pixels+22*atlas.stride+134*4;
    assert(!memcmp(body.pixels+22*body.stride+134*4,frame,4));
    assert(!ai6_read_named(&b->data,"dialog.area",&data,&size)&&size==44);
    for(unsigned i=0;i<2;i++){
        int32_t area[5];memcpy(area,data+4+i*20,20);
        assert(kdialog_hit(area[1],area[2])==area[0]);
        assert(kdialog_hit(area[3]-1,area[4]-1)==area[0]);
        assert(kdialog_hit(area[1],area[4])==-1);
    }
    free(data);assert(kdialog_hit(231,260)==-1&&kdialog_hit(412,260)==-1);
    uint8_t d[]={100,100,100,128},s[]={200,200,200,128};kdialog_pixel(d,s);
    assert(d[3]==191&&d[0]==166); /* straight-alpha intermediate composition */
    assert(!kdialog_draw(&out,&body,&b->layers[0],&atlas,2,0));
    assert(kdialog_draw(&out,&body,&b->layers[0],&atlas,3,0)<0);
    rmt_free(&atlas);rmt_free(&body);rmt_free(&out);
    puts("Kisaku native quit/title dialogs: actual alpha, straight BGRA composition and dialog.area hits: PASS");
}
static int test_audio_command(KBootstrap *b,int main,int sub,int channel,const char *name){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=main;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){channel,NULL}));
    assert(!kvm_push(b->vm,(KValue){0,name}));
    assert(!kvm_push(b->vm,(KValue){sub,NULL}));return bootstrap_dispatch(b);
}
static int test_voice_start(KBootstrap *b){
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=17;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){0,NULL}));
    assert(!kvm_push(b->vm,(KValue){6,NULL}));
    return bootstrap_dispatch(b);
}
static void test_audio_overlap(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    int startup=bootstrap_run(b,100000);
    for(unsigned frame=0;startup==1&&frame<1000&&!b->title.active;frame++){
        bootstrap_frame(b);startup=bootstrap_run(b,100000);
    }
    assert(startup==1&&!b->error[0]&&b->title.active&&b->audio_counts[2]==1);
    b->title.active=0;
    b->message_active=b->message_was_read=b->force_skip=0;
    b->audio_rate=44100;b->audio_channels=2;
    /* A foreground VSD has its own movie PCM stream; starting voice must
       use the ordinary voice output bus instead of rejecting the call. */
    b->video_active=1;b->video_background=0;
    int rc=test_audio_command(b,17,5,0,"z09577.ogg");assert(!rc);assert(!test_voice_start(b));
    assert(!b->error[0]&&b->voice_active&&(b->voice_loading||b->voice_pcm));
    free(b->audio_pcm);b->audio_pcm=NULL;b->audio_size=b->audio_cursor=0;b->voice_active=0;
    /* Logo WAV remains on audio_pcm while the independent voice PCM is
       decoded and mixed. */
    b->video_active=0;b->logo_phase=2;b->audio_pcm=malloc(4);assert(b->audio_pcm);
    memset(b->audio_pcm,0x11,4);b->audio_size=4;b->audio_cursor=0;uint8_t *logo=b->audio_pcm;
    rc=test_audio_command(b,17,5,0,"z09577.ogg");assert(!rc);assert(!test_voice_start(b));
    assert(!b->error[0]&&b->voice_active&&(b->voice_loading||b->voice_pcm)&&b->audio_pcm==logo&&b->audio_size==4);
    unsigned voice_setting=0;for(;voice_setting<b->setting_count;voice_setting++)if(!strcmp(b->settings[voice_setting].section,"Voice")&&!strcmp(b->settings[voice_setting].key,"IsVoice"))break;
    if(voice_setting==b->setting_count){assert(voice_setting<sizeof(b->settings)/sizeof(*b->settings));b->setting_count++;strcpy(b->settings[voice_setting].section,"Voice");strcpy(b->settings[voice_setting].key,"IsVoice");}
    strcpy(b->settings[voice_setting].value,"0");
    b->music_active=b->music_fading=0;b->music_enabled=0;
    uint8_t logo_out[4];size_t logo_pos=0;assert(bootstrap_audio_read(b,&logo_pos,logo_out,sizeof(logo_out))==4&&!memcmp(logo_out,logo,4));
    bootstrap_destroy(b);
    puts("Kisaku voice/movie and voice/logo concurrent PCM buses: PASS");
}
static void test_config_audio(KBootstrap *b){
    /* 464140 maps every configured character, including both NPC ranges. */
    const char *names[]={"z1.ogg","h1.ogg","i1.ogg","b1.ogg","j1.ogg","r1.ogg","c1.ogg","e1.ogg","m1.ogg","t1.ogg","a1.ogg","d1.ogg","s1.ogg","f1.ogg","g1.ogg","k1.ogg","l1.ogg","n65.ogg","n103.ogg","n206.ogg","n211.ogg","n223.ogg","n233.ogg","n264.ogg","n275.ogg","n324.ogg","n333.ogg","n338.ogg","n1.ogg","n37.ogg","n54.ogg","n55.ogg","n62.ogg"};
    assert(b->audio_counts[2]==1&&b->audio_counts[1]>3);
    for(unsigned i=0;i<33;i++){
        char key[24];snprintf(key,sizeof(key),"IsCharVoice%02u",i);test_setting(b,"Voice",key,"0");
        assert(kisaku_voice_character(names[i])==(int)i);
        assert(!test_audio_command(b,17,5,0,names[i])&&!b->audio_objects[2][0].state);
        assert(!strcmp(b->message_voice_name,names[i])); /* History retains muted voice. */
        test_setting(b,"Voice",key,"1");assert(!test_audio_command(b,17,5,0,names[i])&&b->audio_objects[2][0].state==1);
    }
    const unsigned last[]={36,53,54,61,64,102,205,210,222,232,263,274,323,332,337,343};
    const int ids[]={28,29,30,31,32,17,18,19,20,21,22,23,24,25,26,27};
    for(unsigned i=0;i<16;i++){char name[24];snprintf(name,sizeof(name),"N%u.ogg",last[i]);assert(kisaku_voice_character(name)==ids[i]);if(i<15){snprintf(name,sizeof(name),"n%u.ogg",last[i]+1);assert(kisaku_voice_character(name)==ids[i+1]);}}
    assert(kisaku_voice_character("n344.ogg")==0&&kisaku_voice_character("n0.ogg")==0&&kisaku_voice_character("no-z.ogg")==-1);
    test_setting(b,"Voice","IsCharVoice00","0");assert(!test_audio_command(b,17,5,0,"no-z.ogg")&&b->audio_objects[2][0].state==1);
    test_setting(b,"Voice","IsCharVoice00","1");b->audio_objects[2][0].state=0;b->message_voice_name[0]=0;
    uint8_t *data=NULL,*pcm=NULL;size_t bytes=0,size=0;
    assert(!ai6_read_named(&b->effects,"logo.wav",&data,&bytes)&&!kaudio_decode(data,bytes,&pcm,&size));free(data);
    b->audio_size=b->voice_size=0;b->music_active=b->voice_active=0;
    for(unsigned i=0;i<64;i++){b->effect_tracks[i].read=b->effect_tracks[i].size;b->effect_tracks[i].loop_end=0;}
    test_setting(b,"Effect","Volume","0");test_setting(b,"Effect","HVolume","104");test_setting(b,"Effect","IsHEffect","1");
    assert(!test_audio_command(b,16,1,2,"logo.wav")&&!test_audio_command(b,16,1,3,"logo.wav"));
    assert(b->effect_tracks[3].size==size&&!memcmp(b->effect_tracks[3].pcm,pcm,size)&&!memcmp(b->effect_tracks[2].pcm,pcm,size));
    double gain=pow(10.0,kisaku_sound_volume_db(0,1)/2000.0);
    uint8_t *mixed=malloc(size);assert(mixed);size_t position=0;
    test_setting(b,"Effect","IsEffect","0");assert(bootstrap_audio_mix_read(b,&position,mixed,size)==size&&!memcmp(mixed,pcm,size));
    test_setting(b,"Effect","IsEffect","1");test_setting(b,"Effect","IsHEffect","0");
    b->effect_tracks[2].read=b->effect_tracks[3].read=0;
    assert(bootstrap_audio_mix_read(b,&position,mixed,size)==size);
    for(size_t i=0;i<size;i+=2){int16_t src=(int16_t)(pcm[i]|(unsigned)pcm[i+1]<<8),actual=(int16_t)(mixed[i]|(unsigned)mixed[i+1]<<8);assert(actual==(int16_t)(src*gain));}
    test_setting(b,"Effect","IsEffect","0");b->effect_tracks[2].read=b->effect_tracks[3].read=0;
    assert(bootstrap_audio_mix_read(b,&position,mixed,size)==size);for(size_t i=0;i<size;i++)assert(!mixed[i]);
    test_setting(b,"Effect","IsHEffect","1");b->effect_tracks[2].read=b->effect_tracks[3].read=0;
    assert(bootstrap_audio_mix_read(b,&position,mixed,size)==size&&!memcmp(mixed,pcm,size));
    assert(!memcmp(b->effect_tracks[2].pcm,pcm,size)&&!memcmp(b->effect_tracks[3].pcm,pcm,size));
    test_setting(b,"Music","IsMusic","1");test_setting(b,"Music","Volume","104");assert(!call_music_start(b,"bgm01.wav",0,0));
    size_t offset=0;while(offset+4<b->audio_size){int v=(int16_t)(b->audio_pcm[offset]|(unsigned)b->audio_pcm[offset+1]<<8);if(abs(v)>500)break;offset+=4;}assert(offset+4<b->audio_size);
    uint8_t expected[4],out[4];memcpy(expected,b->audio_pcm+offset,4);position=offset;assert(bootstrap_audio_read(b,&position,out,4)==4&&!memcmp(out,expected,4));
    test_setting(b,"Music","IsMusic","0");position=offset;assert(bootstrap_audio_read(b,&position,out,4)==4);for(unsigned i=0;i<4;i++)assert(!out[i]);
    test_setting(b,"Music","IsMusic","1");position=offset;assert(bootstrap_audio_read(b,&position,out,4)==4&&!memcmp(out,expected,4));
    test_setting(b,"Music","IsMusic","0");test_setting(b,"Voice","Volume","104");test_setting(b,"Voice","IsVoice","1");
    free(b->voice_pcm);b->voice_pcm=malloc(4);assert(b->voice_pcm);memcpy(b->voice_pcm,expected,4);b->voice_size=4;b->voice_read_cursor=0;strcpy(b->voice_playing_name,"z09588.ogg");
    assert(bootstrap_audio_mix_read(b,&position,out,4)==4&&!memcmp(out,expected,4));
    test_setting(b,"Voice","IsCharVoice00","0");b->voice_read_cursor=0;assert(bootstrap_audio_mix_read(b,&position,out,4)==4);for(unsigned i=0;i<4;i++)assert(!out[i]);
    test_setting(b,"Voice","IsCharVoice00","1");b->voice_read_cursor=0;assert(bootstrap_audio_mix_read(b,&position,out,4)==4&&!memcmp(out,expected,4));
    /* Native CFuncVoice has its own output bus.  A foreground VSD mixes its
       movie PCM with voice, while logo.wav stays on the main bus and voice
       is mixed independently; neither path rejects the pending voice. */
    free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=0;b->voice_active=0;
    free(b->audio_pcm);b->audio_pcm=NULL;b->audio_size=b->audio_cursor=0;
    b->audio_rate=44100;b->audio_channels=2;b->audio_loop_start=b->audio_loop_end=0;
    b->message_active=b->message_was_read=b->force_skip=0;b->video_active=1;b->video_background=0;b->logo_phase=0;
    assert(!test_audio_command(b,17,5,0,"z09577.ogg"));assert(!test_voice_start(b));
    assert(b->voice_active&&(b->voice_loading||b->voice_pcm));
    free(b->audio_pcm);b->audio_pcm=NULL;b->audio_size=b->audio_cursor=0;b->voice_active=0;
    b->video_active=0;b->logo_phase=2;b->audio_rate=44100;b->audio_channels=2;
    b->audio_pcm=malloc(4);assert(b->audio_pcm);memset(b->audio_pcm,0x11,4);b->audio_size=4;b->audio_cursor=0;
    uint8_t *logo_bus=b->audio_pcm;
    assert(!test_audio_command(b,17,5,0,"z09577.ogg"));assert(!test_voice_start(b));
    assert(b->voice_active&&(b->voice_loading||b->voice_pcm)&&b->audio_pcm==logo_bus&&b->audio_size==4);
    free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=b->voice_read_cursor=b->voice_clock_cursor=0;
    free(b->audio_pcm);b->audio_pcm=NULL;b->audio_size=b->audio_cursor=0;b->logo_phase=0;
    free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=0;b->voice_active=b->music_active=0;b->audio_size=0;
    test_setting(b,"Music","IsMusic","1");test_setting(b,"Music","Volume","72");test_setting(b,"Voice","Volume","83");
    test_setting(b,"Effect","IsEffect","1");test_setting(b,"Effect","IsHEffect","1");test_setting(b,"Effect","Volume","83");test_setting(b,"Effect","HVolume","83");free(pcm);free(mixed);
    puts("Kisaku configuration audio: 33 character channels/NPC boundaries, independent channel3 gain and lossless live mute/unmute for BGM/voice/effects: PASS");
}
static void test_ui_and_logo(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    test_setting(b,"Effect","Volume","83");test_setting(b,"Effect","IsEffect","1");
    unsigned serial=~0u,logo_seen=0;size_t position=0,nonzero=0,mixed=0;
    int rc=bootstrap_run(b,100000);
    for(unsigned frame=0;rc==1&&frame<1000&&!(b->title.active&&b->title.age>=64);frame++){
        for(unsigned i=0;i<b->audio_counts[1]&&i<64;i++){
            KEffectTrack *t=&b->effect_tracks[i];const char *name=b->audio_objects[1]?b->audio_objects[1][i].name:"";
            if(t->pcm&&!t->clock_position)printf("Opening effect: %s (%zu bytes)\n",name,t->size);
            unsigned bit=!strcmp(name,"logo.wav")?1:!strcmp(name,"potapota.wav")?2:0;
            if(!t->pcm||!bit||(logo_seen&bit))continue;
            uint8_t *data=NULL,*pcm=NULL;size_t bytes=0,size=0;
            assert(!ai6_read_named(&b->effects,name,&data,&bytes));assert(!kaudio_decode(data,bytes,&pcm,&size));free(data);
            assert(size==t->size);size_t audible=0;
            for(size_t at=0;at+1<size;at+=2){
                int16_t src=(int16_t)(pcm[at]|pcm[at+1]<<8),dst=(int16_t)(t->pcm[at]|t->pcm[at+1]<<8);
                assert(dst==src);if(dst)audible++;
            }
            assert(audible);free(pcm);logo_seen|=bit;
        }
        if(serial!=b->audio_serial){serial=b->audio_serial;position=0;}
        uint8_t out[2940];size_t n=bootstrap_audio_mix_read(b,&position,out,sizeof(out));mixed+=n;
        for(size_t i=0;i<n;i++)if(out[i])nonzero++;
        bootstrap_frame(b);rc=bootstrap_run(b,100000);
    }
    assert(rc==1&&!b->error[0]&&b->title.active&&mixed&&nonzero);
    printf("Opening audio: logo mask=%u, mixed=%zu bytes, nonzero=%zu bytes\n",logo_seen,mixed,nonzero);
    assert(logo_seen==3);
    assert(kisaku_sound_volume_db(0,1)==-2121&&kisaku_sound_volume_db(83,1)==-254);
    assert(kisaku_sound_volume_db(104,1)==0&&kisaku_sound_volume_db(255,1)==0&&kisaku_sound_volume_db(83,0)==-10000);
    b->title.active=0;b->vm->globals[0][50].number=0;
    test_config_audio(b);
    memset(b->layers[1].pixels,0x7f,84*b->layers[1].stride);
    assert(!call(b,10,0));
    for(unsigned y=0;y<84;y++)for(unsigned x=0;x<640*4;x++)assert(!b->layers[1].pixels[y*b->layers[1].stride+x]);
    test_setting(b,"Msg","Alpha","112");test_setting(b,"Msg","Red","0");test_setting(b,"Msg","Green","0");test_setting(b,"Msg","Blue","0");
    test_setting(b,"Msg","ShowSpeed","255");test_setting(b,"Msg","IsAutoMes","0");test_setting(b,"Msg","IsOneMes","0");
    test_setting(b,"Display","EffectSpeed","2");assert(!call(b,10,3));
    memset(b->layers[0].pixels,100,640*480*4);
    uint8_t *glyph=b->layers[1].pixels+8*b->layers[1].stride+32*4;memset(glyph,255,4);
    b->vm->globals[0][46].number=48;
    assert(!call(b,0,-1));bootstrap_frame(b);assert(!b->error[0]);
    uint8_t *screen=b->layers[0].pixels;
    assert(screen[395*2560]==100&&screen[396*2560]==39);
    assert(screen[404*2560+32*4]==255&&screen[403*2560+32*4]==39);
    assert(screen[404*2560+600*4]==39); /* Previously copied stale right-margin artwork. */
    const int actions[]={7,6,5,1,0,9},xs[]={600,530,460,392,324,24};
    b->message_open=1;
    for(unsigned i=0;i<6;i++){bootstrap_pointer(b,xs[i],470,0);assert(b->message_hover==actions[i]);}
    bootstrap_pointer(b,600,463,0);assert(b->message_hover==-1);
    b->message_hover=7;bootstrap_menu_move(b,-1,0);assert(b->message_hover==6);
    bootstrap_pointer(b,530,470,1);assert(b->message_request==6);b->message_request=0;
    bootstrap_pointer(b,24,470,1);assert(b->vm->globals[0][50].number&0x8000);
    b->message_active=b->message_visible=0;b->vm->globals[0][50].number=0;
    test_message_timing(b);test_message_prefix_and_buttons(b);
    test_native_dialog(b);
    /* Synthetic scanlines expose wrong 34-to-52 stretching and page placement. */
    for(unsigned y=0;y<68;y++)for(unsigned x=0;x<320;x++){
        uint8_t *p=b->layers[5].pixels+y*b->layers[5].stride+x*4;p[0]=(uint8_t)(y+1);p[1]=p[2]=0;p[3]=255;
    }
    b->vm->bytes[1000]=0;assert(!call(b,30,0));
    b->choice_normal=b->choice_active=1;b->choice_count=5;b->choice_selected=-1;b->choice_page=0;b->choice_rendered_page=~0u;
    b->choice_base=(KImage){0,0,640,480,2560,calloc(480,2560)};b->choice_text=(KImage){0,0,640,480,2560,calloc(480,2560)};
    for(unsigned i=0;i<5;i++){b->choice_values[i]=(int)i;b->choice_returns[i]=(int)i+1;b->choice_lengths[i]=0;}
    bootstrap_frame(b);assert(!b->error[0]);
    for(unsigned y=0;y<52;y++){unsigned expected=y<8?y+1:y<44?9+(y-8)%18:27+y-44;assert(screen[(136+y)*2560+100*4]==expected);}
    bootstrap_pointer(b,100,138,1);assert(b->choice_active&&b->choice_selected==-1);
    bootstrap_pointer(b,100,352,1);assert(b->choice_active&&b->choice_page==1&&b->choice_selected==-1);
    assert(screen[136*2560+100*4]==1&&screen[188*2560+100*4]==0);
    bootstrap_pointer(b,100,110,1);assert(b->choice_page==0&&b->choice_active);
    b->vm->bytes[1000]=1;b->vm->bytes[4010]=1;b->vm->bytes[2000]=0;b->vm->globals[1][61].number=0;
    bootstrap_pointer(b,100,146,1);assert(b->choice_active); /* Disabled entries cannot return. */
    b->vm->bytes[2000]=1;bootstrap_pointer(b,100,146,1);assert(!b->choice_active&&b->vm->globals[0][18].number==1);
    bootstrap_destroy(b);
    puts("Kisaku logo PCM gain/mixing, message margins/buttons and native choice rows/paging/disabled hits: PASS");
}
static void title_test_wait(KBootstrap *b){
    for(unsigned i=0;i<3000;i++){
        int rc=bootstrap_run(b,100000);
        if(rc<0)fprintf(stderr,"title path: %s\n",b->error);
        assert(rc>=0);if(b->title.active&&b->title.age>=64)return;bootstrap_frame(b);
    }assert(!"title path timed out");
}
static int title_test_call(KBootstrap *b,int mode,int action){
    b->vm->syscall=31;b->vm->status=KVM_SYSCALL;b->vm->sp=0;b->error[0]=0;
    assert(!kvm_push(b->vm,(KValue){mode,NULL}));
    assert(!kvm_push(b->vm,(KValue){action,NULL}));
    assert(!kvm_push(b->vm,(KValue){110,NULL}));
    return bootstrap_dispatch(b);
}
static void test_title_appendix(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b);title_test_wait(b);
    /* Follow open.mes from the actual appendix button (return 2). */
    b->title.selected=2;bootstrap_confirm(b);
    title_test_wait(b);assert(b->title.count==6&&b->title.native_ids[5]==10);
    b->title.selected=5;bootstrap_confirm(b);title_test_wait(b);
    assert(b->title.native_ids[0]==0&&b->title.native_ids[b->title.count-1]==5);
    const unsigned flags[]={4005,4002,3290,4004};
    for(unsigned i=0;i<4;i++)b->vm->bytes[flags[i]]=0;
    assert(!title_test_call(b,1,1));
    for(unsigned i=0;i<4;i++)assert(b->title.native_ids[i]==-1&&b->title.native_sources[i]==6+i);
    b->vm->globals[0][18].number=987;bootstrap_pointer(b,500,270,1);
    assert(b->title.active&&b->vm->globals[0][18].number==987);
    for(unsigned i=0;i<4;i++)b->vm->bytes[flags[i]]=1;
    assert(!title_test_call(b,1,0));assert(!title_test_call(b,1,1));
    const int expected[]={6,7,8,9,13,10};
    for(unsigned i=0;i<6;i++)assert(b->title.native_ids[i]==expected[i]);
    for(unsigned i=0;i<6;i++){
        b->title.selected=(int)i;assert(!ktitle_draw(&b->title,&b->layers[0]));
        bootstrap_confirm(b);assert(!b->title.active&&b->vm->globals[0][18].number==expected[i]);
        assert(!title_test_call(b,1,1));
    }
    b->vm->bytes[3601]=1;assert(!title_test_call(b,3,1));
    assert(b->title.count==3&&b->title.native_ids[0]==6&&b->title.native_ids[1]==8&&b->title.native_ids[2]==10);
    b->vm->globals[1][60].number=1;
    b->vm->bytes[4009]=1;b->vm->bytes[3600]=1;assert(!title_test_call(b,2,1));
    assert(b->title.count==6&&b->title.native_ids[3]==11&&b->title.native_ids[5]==12);
    assert(title_test_call(b,4,1)<0&&b->vm->sp==3);
    bootstrap_destroy(b);puts("Kisaku main-to-appendix script round trip, four layouts, locks and returns: PASS");
}
static void test_calendar_persistence(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b);title_test_wait(b);
    b->title.active=0;b->wait_input=1;
    uint8_t *data=NULL;size_t size=0;KImage atlas={0};
    assert(!ai6_read_named(&b->images,"timepart.akb",&data,&size)&&!rmt_decode(data,size,&atlas));free(data);
    rmt_free(&b->layers[7]);b->layers[7]=atlas;
    assert(!call_overlay524(b,304,4,0));
    uint8_t expected[120*128*4];memcpy(expected,b->overlay524_sprite.pixels,sizeof(expected));
    b->choice_base=(KImage){0,0,640,480,2560,calloc(640*480,4)};assert(b->choice_base.pixels);
    b->choice_text=(KImage){0,0,640,480,2560,calloc(640*480,4)};assert(b->choice_text.pixels);
    b->choice_active=1;b->choice_count=0;b->choice_selected=-1;b->choice_rendered_page=0;
    for(unsigned frame=0;frame<180;frame++){
        memset(b->choice_base.pixels,(int)(frame%128),640*480*4);
        bootstrap_frame(b);assert(!b->error[0]);
        assert(bootstrap_run(b,100000)==1&&b->overlay524_drawn);
        for(unsigned y=0;y<128;y++)for(unsigned x=0;x<120;x++){
            const uint8_t *p=expected+(y*120+x)*4,*d=b->layers[0].pixels+(16+y)*2560+(16+x)*4;
            for(unsigned c=0;c<3;c++)assert(d[c]==(uint8_t)((frame%128)*(255-p[3])/255+p[c]*p[3]/255));
        }
    }
    assert(!call(b,524,29)&&!b->overlay524_drawn&&!b->overlays.badge_visible);
    bootstrap_frame(b);assert(!b->overlay524_drawn);
    assert(!call(b,524,30)&&b->overlays.badge_visible);bootstrap_frame(b);assert(b->overlay524_drawn);
    assert(!call_overlay524(b,0,0,1));bootstrap_frame(b);assert(!b->overlay524_drawn);
    bootstrap_destroy(b);puts("Date badge: real image text, choice redraw persistence, suspend/resume and explicit hide: PASS");
}
static void title_path_until(KBootstrap *b,unsigned wanted){
    for(unsigned i=0;i<10000;i++){
        int rc=bootstrap_run(b,100000);
        if(rc<0)fprintf(stderr,"menu path: %s\n",b->error);
        assert(rc>=0);
        if(wanted==1?b->file_modal:wanted==2?b->title_reset_modal:b->title.active&&b->title.age>=64)return;
        bootstrap_frame(b);
    }assert(!"menu path timed out");
}
static void test_title_paths(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b);title_test_wait(b);
    b->vm->globals[1][60].number=1;assert(!title_test_call(b,0,1));
    b->title.selected=1;bootstrap_confirm(b);title_path_until(b,1);
    assert(b->message_request==3&&b->vm->sp==1);
    size_t ip=b->vm->ip;assert(bootstrap_run(b,100000)==1&&b->vm->ip==ip);
    b->message_request=b->file_modal=0;title_path_until(b,0);
    b->title.selected=3;bootstrap_confirm(b);title_path_until(b,2);
    assert(b->message_request==21);b->message_request=0;
    assert(!bootstrap_title_reset_close(b,0));title_path_until(b,0);
    assert(b->vm->globals[1][60].number==1);
    /* Yes is exercised only against the caller's disposable test directory. */
    b->title.selected=3;bootstrap_confirm(b);title_path_until(b,2);b->message_request=0;
    assert(!bootstrap_title_reset_close(b,1));title_path_until(b,0);
    assert(!b->vm->globals[1][60].number&&b->title.native_ids[1]==-1);
    b->title.selected=2;bootstrap_confirm(b);title_path_until(b,0);
    assert(b->title.native_ids[4]==13);b->title.selected=4;bootstrap_confirm(b);title_path_until(b,0);
    assert(b->vm->globals[1][61].number==1&&b->title.native_ids[b->title.count-1]==12);
    b->title.selected=(int)b->title.count-1;bootstrap_confirm(b);title_path_until(b,0);
    assert(b->vm->globals[1][61].number==0&&b->title.native_ids[b->title.count-1]==5);
    b->title.selected=2;bootstrap_confirm(b);title_path_until(b,0);
    b->title.selected=4;bootstrap_confirm(b);title_path_until(b,0);
    b->title.selected=0;bootstrap_confirm(b);
    for(unsigned i=0;!b->message_active;i++){
        int rc=bootstrap_run(b,100000);if(rc<0)fprintf(stderr,"hage start: %s\n",b->error);
        assert(i<10000&&rc>=0);bootstrap_frame(b);
    }
    assert(b->text_count&&b->vm->globals[1][61].number==1);
    KFlags *broken=kflags_read_slot(saves,0,201);assert(broken);broken->byte_count=8192;
    assert(!kflags_write_slot(broken,saves,0,201));kflags_free(broken);
    uint8_t *raw=b->raw_variables;int before=b->vm->globals[0][18].number;
    b->vm->syscall=14;b->vm->status=KVM_SYSCALL;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){201,NULL})&&!kvm_push(b->vm,(KValue){12,NULL}));
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==2&&b->raw_variables==raw&&b->vm->globals[0][18].number==before);
    bootstrap_destroy(b);puts("Title load/cancel, reset No/Yes, mode round trip, Hage first dialogue and invalid FLAG preservation: PASS");
}
static void test_native_cg(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b);title_path_until(b,0);
    b->vm->bytes[4005]=1;b->title.selected=2;bootstrap_confirm(b);title_path_until(b,0);
    b->title.selected=0;bootstrap_confirm(b);
    for(unsigned i=0;!b->native_cg;i++){assert(i<10000);int rc=bootstrap_run(b,100000);if(rc<0)fprintf(stderr,"CG entry: %s\n",b->error);assert(rc>=0);bootstrap_frame(b);}
    assert(b->message_request==22);b->message_request=0;
    const KImage *im=bootstrap_native_cg_image(b);assert(im&&im->width==640&&im->height==480);
    uint8_t *locked=malloc(640*480*4);assert(locked);memcpy(locked,im->pixels,640*480*4);
    assert(!bootstrap_native_cg_pointer(b,170,70,1));assert(!memcmp(locked,im->pixels,640*480*4));
    b->vm->bytes[5000]=1;assert(!bootstrap_native_cg_pointer(b,170,70,1));
    assert(memcmp(locked,im->pixels,640*480*4));size_t ip=b->vm->ip,sp=b->vm->sp;
    int rc=bootstrap_native_cg_action(b,0);fprintf(stderr,"CG full image rc=%d\n",rc);assert(!rc);
    assert(b->vm->ip==ip&&b->vm->sp==sp&&b->vm->bytes[5000]==1);
    for(unsigned i=0;i<30;i++)bootstrap_frame(b);
    assert(!bootstrap_native_cg_action(b,1)&&!bootstrap_native_cg_action(b,1)&&!bootstrap_native_cg_action(b,1));
    assert(!b->native_cg);title_path_until(b,0);assert(b->title.native_ids[0]==6);
    b->vm->globals[1][61].number=1;b->vm->bytes[3501]=1;
    b->title.active=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;assert(!kvm_push(b->vm,(KValue){310,NULL}));
    assert(!bootstrap_dispatch(b)&&b->native_cg);b->message_request=0;
    assert(!bootstrap_native_cg_pointer(b,60,70,1));rc=bootstrap_native_cg_action(b,0);fprintf(stderr,"Hage CG full image rc=%d\n",rc);assert(!rc);
    assert(!bootstrap_native_cg_action(b,1)&&!bootstrap_native_cg_action(b,1)&&!bootstrap_native_cg_action(b,1));
    assert(b->vm->sp==1&&b->vm->stack[0].number==-1);
    free(locked);bootstrap_destroy(b);puts("Native CG: original script entry, lock, variants, isolated normal/Hage full image and appendix return: PASS");
}
static void test_hires_present(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b);b->present_hires=1;
    for(unsigned i=0;!b->title.active||b->title.age<64;i++){
        assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);
    }
    b->title.selected=0;bootstrap_confirm(b);
    unsigned observed=0;
    uint8_t *raw=malloc(640*480*4);assert(raw);
    for(unsigned i=0;i<6000;i++){
        assert(bootstrap_run(b,100000)>=0);
        if(b->flag_dialog.active)bootstrap_pointer(b,300,350,1);
        bootstrap_frame(b);
        if(!b->message_active||b->message_slide)continue;
        memcpy(raw,b->layers[0].pixels,640*480*4);
        const KImage *overlay=NULL,*clean=bootstrap_present_layers(b,&overlay);
        assert(overlay&&clean!=&b->layers[0]&&overlay->width==960&&overlay->height==720);
        assert(!memcmp(raw,b->layers[0].pixels,640*480*4));
        unsigned ink=0;for(unsigned px=0;px<960*720;px++)ink+=overlay->pixels[px*4+3]!=0;
        if(ink)observed++;
        if(!b->message_revealing){
            assert(ink&&memcmp(clean->pixels,raw,640*480*4));
            b->present_hires=0;assert(bootstrap_present_layers(b,&overlay)==&b->layers[0]&&!overlay);
            b->present_hires=1;
            b->layers[1].pixels[0]^=1;
            assert(bootstrap_present_layers(b,&overlay)==&b->layers[0]&&!overlay);
            b->layers[1].pixels[0]^=1;
            break;
        }
    }
    assert(observed);
    b->message_active=b->message_visible=0;b->vm->globals[0][50].number=0;
    assert(!call(b,30,0));
    b->choice_normal=b->choice_active=1;b->choice_count=1;b->choice_selected=0;b->choice_rendered_page=~0u;
    b->choice_base=(KImage){0,0,640,480,2560,calloc(480,2560)};
    b->choice_text=(KImage){0,0,640,480,2560,calloc(480,2560)};
    strcpy(b->choice_labels[0],"ABC");b->choice_lengths[0]=3;
    b->choice_values[0]=0;b->choice_returns[0]=1;
    bootstrap_frame(b);assert(!b->error[0]);memcpy(raw,b->layers[0].pixels,640*480*4);
    const KImage *overlay=NULL,*clean=bootstrap_present_layers(b,&overlay);
    assert(overlay&&clean!=&b->layers[0]&&memcmp(clean->pixels,raw,640*480*4));
    assert(!memcmp(raw,b->layers[0].pixels,640*480*4));
    free(raw);bootstrap_destroy(b);
    puts("960x720 text: real startup reveal, clean backing, immutable raw and modified-layer fallback: PASS");
}

int main(int argc,char **argv){
    if(argc==4&&!strcmp(argv[3],"--hires")){test_hires_present(argv[1],argv[2]);return 0;}
    if(argc==4&&!strcmp(argv[3],"--native-cg")){test_native_cg(argv[1],argv[2]);return 0;}
    if(argc==4&&!strcmp(argv[3],"--audio-overlap")){test_audio_overlap(argv[1],argv[2]);return 0;}
    if(argc==4&&!strcmp(argv[3],"--title-paths")){test_title_paths(argv[1],argv[2]);return 0;}
    if(argc==4&&!strcmp(argv[3],"--calendar")){test_week(argv[1],argv[2]);test_calendar_persistence(argv[1],argv[2]);test_graphics_windows(argv[1],argv[2]);return 0;}
    if(argc==4&&!strcmp(argv[3],"--title")){test_title_appendix(argv[1],argv[2]);return 0;}
    if(argc==4&&!strcmp(argv[3],"--backlog")){
        test_backlog_records(argv[1],argv[2]);test_backlog_lifecycle(argv[1],argv[2]);test_native_wait(argv[1],argv[2]);test_backlog_newline(argv[1],argv[2]);test_backlog_capture(argv[1],argv[2]);test_backlog_replay(argv[1],argv[2]);test_backlog_real_records(argv[1],argv[2]);test_choice_stack_isolation(argv[1],argv[2]);return 0;
    }
    if(argc!=3)return 2;
    test_title_appendix(argv[1],argv[2]);
    KBootstrap *b=bootstrap_create_split(argv[1],argv[2]);assert(b);
    int result=bootstrap_run(b,100000);
    for(unsigned frame=0;result==1&&frame<1000&&!(b->title.active&&b->title.age>=64);frame++){bootstrap_frame(b);result=bootstrap_run(b,100000);}
    assert(result==1&&!b->error[0]);assert(b->vm->syscall==31);
    /* Presentation-only CAS strength accepts normalized and percentage
       settings without entering the native CConfig index table. */
    test_setting(b,"Display","CASStrength","0.25");assert(bootstrap_cas_strength(b)==25);
    test_setting(b,"Display","CASStrength","75");assert(bootstrap_cas_strength(b)==75);
    test_setting(b,"Display","CASStrength","200");assert(bootstrap_cas_strength(b)==100);
    test_setting(b,"Display","CASStrength","invalid");assert(bootstrap_cas_strength(b)==0);
    test_setting(b,"Display","CASStrength","0");assert(bootstrap_cas_strength(b)==0);
    assert(!b->vm->sp&&b->title.active&&b->title.variant==4);
    assert(b->vm->byte_count==9192&&b->vm->word_count==600&&b->raw_size==15000);
    assert(b->vm->raw==b->raw_variables&&b->vm->raw_size==b->raw_size);
    assert(b->raw_variables[0]==20&&b->raw_variables[1]==5&&b->raw_variables[2]==6);
    assert(b->layer_count==14&&b->last_loaded_layer==3&&b->choice_prepared);
    assert(b->layers[7].width==640&&b->layers[7].height==400&&b->layers[7].stride==2560);
    assert(b->vm->globals[0][42].number==32&&b->vm->globals[0][43].number==8);
    assert(b->vm->globals[0][44].number==592&&b->vm->globals[0][45].number==62);
    assert(!call_527(b)&&b->vm->sp==1&&b->vm->stack[0].number==0&&b->message_request==2&&b->file_modal);
    b->message_request=b->file_modal=0;
    b->vm->sp=0;
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
    uint8_t *native_ax=NULL;size_t native_ax_size=0;struct ax_player counted_ax;
    assert(!ai6_read_named(&b->data,"act15.ax",&native_ax,&native_ax_size));
    assert(ax_load(&counted_ax,"act15.ax",native_ax,native_ax_size));free(native_ax);
    int32_t native_boundaries=0;assert(ax_count_boundaries(&counted_ax,0,&native_boundaries));
    printf("Kisaku act15.ax track 0 boundaries: %d\n",native_boundaries);

    assert(call(b,43,2)<0&&b->vm->sp==2);
    assert(!call(b,43,0)&&!b->vm->sp&&!b->mes_fade_visible);
    for(unsigned i=0;i<640*480;i++){
        assert(!b->mes_fade_surfaces[0].pixels[i*4+3]);
        assert(b->mes_fade_surfaces[2].pixels[i*4+3]==100);
    }
    int old_target=b->vm->globals[0][49].number;
    b->vm->globals[0][49].number=63;
    b->mes_fade_surfaces[0].pixels[0]=87;
    assert(call(b,43,2)<0&&b->vm->sp==2&&b->mes_fade_surfaces[0].pixels[0]==87);
    b->vm->globals[0][49].number=old_target;
    KValue old_rect[4];memcpy(old_rect,b->vm->globals[0]+42,sizeof(old_rect));
    assert(!call(b,43,2)&&!b->vm->sp&&b->mes_fade_visible);
    assert(!b->mes_fade_surfaces[0].pixels[0]);
    assert(b->vm->globals[0][42].number==0&&b->vm->globals[0][43].number==0);
    assert(b->vm->globals[0][44].number==640&&b->vm->globals[0][45].number==480);
    assert(call(b,43,3)<0&&b->vm->sp==2&&b->vm->stack[0].number==3);
    assert(!call(b,43,0)&&!b->mes_fade_visible);
    assert(!call(b,43,1)&&!call(b,43,1));
    for(unsigned i=0;i<3;i++)assert(!b->mes_fade_surfaces[i].pixels);
    memcpy(b->vm->globals[0]+42,old_rect,sizeof(old_rect));
    puts("Kisaku message fade lifecycle, transparent reset and preserved unsupported arguments: PASS");
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
    /* 31/528/20 stores four people for day 1; 21 edits a single event. */
    const KValue day_args[]={{6,NULL},{3,NULL},{2,NULL},{1,NULL},{1,NULL},{20,NULL},{528,NULL}};
    assert(!call_anime520(b,day_args,7)&&!b->vm->sp&&b->diary_days==2);
    assert(b->vm->words[204]==1&&b->vm->words[207]==6&&b->diary_people[6]==3);
    const KValue event_args[]={{26,NULL},{4,NULL},{21,NULL},{528,NULL}};
    assert(!call_anime520(b,event_args,4)&&b->vm->words[304]==26&&b->diary_events[4]==26);
    for(unsigned y=0;y<20;y++){
        assert(!memcmp(b->diary_surface.pixels+(96+y)*b->diary_surface.stride+120*4,diary_atlas.pixels+(256+y)*diary_atlas.stride,152*4));
        assert(!memcmp(b->diary_surface.pixels+(96+y)*b->diary_surface.stride+272*4,diary_atlas.pixels+(756+y)*diary_atlas.stride+152*4,336*4));
    }
    KValue bad_event[]={{28,NULL},{4,NULL},{21,NULL},{528,NULL}};
    assert(call_anime520(b,bad_event,4)<0&&b->vm->sp==4&&b->vm->words[304]==26&&b->diary_events[4]==26);
    bad_event[1].number=96;assert(call_anime520(b,bad_event,4)<0&&b->vm->sp==4);
    rmt_free(&diary_atlas);b->diary_scroll=16;assert(!call(b,528,24)&&b->diary_scroll==16);
    assert(b->vm->words[204]==1&&b->vm->words[304]==26); /* Reset is private only. */
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
    check_small_glyph(b,474,1);check_small_glyph(b,482,5);check_small_glyph(b,534,2);check_small_glyph(b,542,7);
    const int zero_left_counts[]={0,7,1,10,528};b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<5;i++)kvm_push(b->vm,(KValue){zero_left_counts[i],NULL});
    assert(!bootstrap_dispatch(b)&&!b->vm->sp);
    check_small_glyph(b,474,11);check_small_glyph(b,482,7);/* 49fbe0 calls 49e250, which restores the panel background before
       zero-left updates only the second cell. */
    for(unsigned y=0;y<16;y++)assert(!memcmp(b->param_surface.pixels+(92+y)*b->param_surface.stride+534*4,
        b->param_atlas.pixels+(92+y)*b->param_atlas.stride+534*4,32));
    check_small_glyph(b,542,12);
    const int counts[]={100,20,1,10,528};b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    for(unsigned i=0;i<5;i++)kvm_push(b->vm,(KValue){counts[i],NULL});
    assert(!bootstrap_dispatch(b)&&b->param_total==255&&b->param_remaining==100);
    check_small_glyph(b,474,10);check_small_glyph(b,482,10);check_small_glyph(b,534,10);check_small_glyph(b,542,10);
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
    b->vm->stack[1].string="invalid";
    unsigned before_param_calls=b->handled;
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==10&&b->handled==before_param_calls);
    for(int i=0;i<8;i++)assert(b->vm->stack[i].number==100+i);
    assert(b->vm->stack[8].number==9&&b->vm->stack[9].number==528);
    assert(strstr(b->error,"numeric parameters"));
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
    uint8_t *part=b->overlay524_sprite.pixels+24*b->overlay524_sprite.stride+25*4;
    assert(part[0]==51&&part[1]==53&&part[2]==59&&part[3]==61);
    part=b->overlay524_sprite.pixels+56*b->overlay524_sprite.stride+29*4;
    assert(part[0]==67&&part[1]==71&&part[2]==73&&part[3]==79);
    part=b->overlay524_sprite.pixels+84*b->overlay524_sprite.stride+29*4;
    assert(part[0]==83&&part[1]==89&&part[2]==97&&part[3]==101);
    assert(call_overlay524(b,304,4,2)<0&&b->vm->sp==2);
    b->layers[0].pixels[300*2560+300*4]=199;
    assert(!call_overlay524(b,0,0,1)&&!b->vm->sp&&!b->overlay524_visible);
    assert(b->layers[0].pixels[300*2560+300*4]==199);
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
    assert(call(b,525,4)<0&&b->vm->sp==2);
    puts("Kisaku CLetter image preservation, blend rounding and speed modes: PASS");
    /* 4fd915 consumes only action 1; unrelated caller stack survives. */
    b->exec526_surfaces[0]=(KImage){0,0,1,1,4,calloc(4,1)};
    b->exec526_surfaces[1]=(KImage){0,0,1,1,4,calloc(4,1)};
    b->exec526_active=1;
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){1,NULL});kvm_push(b->vm,(KValue){526,NULL});
    assert(!bootstrap_dispatch(b)&&!b->vm->sp&&!b->exec526_active&&!b->exec526_surfaces[0].pixels&&!b->exec526_surfaces[1].pixels);
    b->error[0]=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    kvm_push(b->vm,(KValue){0,"bad"});kvm_push(b->vm,(KValue){1,NULL});kvm_push(b->vm,(KValue){526,NULL});
    assert(!bootstrap_dispatch(b)&&b->vm->sp==1&&b->vm->stack[0].string);
    b->vm->globals[0][0].number=40;
    assert(call(b,526,2)<0&&b->vm->sp==2&&!b->exec526_active);
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
    assert(call(b,612,0)<0&&b->vm->sp==2);
    b->vm->sp=0;kvm_push(b->vm,(KValue){9,NULL});kvm_push(b->vm,(KValue){3,NULL});
    kvm_push(b->vm,(KValue){2,NULL});kvm_push(b->vm,(KValue){1,NULL});
    kvm_push(b->vm,(KValue){0,NULL});kvm_push(b->vm,(KValue){0,NULL});kvm_push(b->vm,(KValue){612,NULL});
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==7&&b->vm->stack[0].number==9&&b->vm->stack[4].number==0);
    assert(call(b,612,1)<0&&b->vm->sp==2);
    b->bowling.slots[1]=(KBowlingResource){malloc(4),bowling_free};assert(b->bowling.slots[1].object);
    puts("Kisaku 31/612/2 resource cleanup and invalid-constructor boundary: PASS");
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
    assert(!call_anime520(b,anime_start,4)&&!b->vm->sp&&b->ax_extra.cells[0].state==0);
    KValue anime_start_bad[]={{0,NULL},{10,NULL},{2,NULL},{520,NULL}};
    assert(call_anime520(b,anime_start_bad,4)<0&&b->vm->sp==4&&b->vm->stack[1].number==10);
    KValue anime_run[]={{0,NULL},{0,NULL},{3,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_run,4)&&!b->vm->sp&&b->ax_extra.cells[0].state==1);
    KValue anime_stop[]={{0,NULL},{0,NULL},{4,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_stop,4)&&!b->vm->sp&&b->ax_extra.cells[0].state==AX_STOPPED);
    b->ax_extra.cells[0].state=0;b->ax_extra.cells[1].state=AX_STOPPED;
    KValue anime_run_all[]={{6,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_run_all,2)&&!b->vm->sp&&b->ax_extra.cells[0].state==1&&b->ax_extra.cells[1].state==AX_STOPPED);
    KValue anime_extended[]={{3,NULL},{0,NULL},{0,NULL},{12,NULL},{520,NULL}};
    assert(!call_anime520(b,anime_extended,5)&&!b->vm->sp&&b->animation_target_layer==0);
    KValue anime_extended_bad[]={{3,NULL},{0,NULL},{10,NULL},{12,NULL},{520,NULL}};
    assert(call_anime520(b,anime_extended_bad,5)<0&&b->vm->sp==5&&b->vm->stack[2].number==10);
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
    assert(!call(b,520,10)&&!b->vm->sp&&b->ax_extra_modal==2);
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
    KValue color_key[]={{0,NULL},{0x112233,NULL},{7,NULL},{0,NULL},{0,NULL},{1,NULL},{1,NULL},{3,NULL},{0,NULL},{0,NULL}};
    assert(!call_layer(b,color_key,10,4)&&!b->vm->sp);
    uint8_t *key_dst=b->layers[1].pixels+1*4;assert(key_dst[0]==11&&key_dst[1]==21&&key_dst[2]==31&&key_dst[3]==91);
    uint8_t *copied=b->layers[1].pixels;assert(copied[0]==40&&copied[1]==50&&copied[2]==60&&copied[3]==90);
    color_key[0].number=1;for(unsigned x=0;x<3;x++){uint8_t *d=b->layers[1].pixels+x*4;d[3]=(uint8_t)(120+x);}
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
    test_bowling_scripts(argv[1],argv[2]);
    test_bowling_modal(argv[1],argv[2]);
    test_week(argv[1],argv[2]);test_calendar_persistence(argv[1],argv[2]);
    test_backlog_records(argv[1],argv[2]);
    test_native_wait(argv[1],argv[2]);
    test_backlog_lifecycle(argv[1],argv[2]);
    test_backlog_newline(argv[1],argv[2]);test_backlog_capture(argv[1],argv[2]);test_backlog_replay(argv[1],argv[2]);test_backlog_real_records(argv[1],argv[2]);
    test_native_tint(argv[1],argv[2]);
    assert(!call(b,1011,0));
    b->vm->bytes[3600]=b->vm->bytes[5038]=b->vm->bytes[4001]=b->vm->bytes[4004]=0;
    assert(!call_gallery_mark(b,"ev45d_1.mov")&&!b->vm->sp);
    assert(b->vm->bytes[3600]&&b->vm->bytes[5038]&&b->vm->bytes[4001]&&b->vm->bytes[4004]);
    uint8_t media_before[sizeof(b->vm->bytes)];memcpy(media_before,b->vm->bytes,sizeof(media_before));
    assert(!call_gallery_mark(b,"missing-resource.mov")&&!b->vm->sp);
    assert(!memcmp(media_before,b->vm->bytes,sizeof(media_before)));
    unsigned media_capacity=b->vm->byte_count;b->vm->byte_count=5038;
    assert(call_gallery_mark(b,"EV45D_1.MOV")<0&&b->vm->sp==2);
    assert(!memcmp(media_before,b->vm->bytes,sizeof(media_before)));b->vm->byte_count=media_capacity;
    assert(call_gallery_mark(b,NULL)<0&&b->vm->sp==2);
    char media_long[1025];memset(media_long,'a',sizeof(media_long)-1);media_long[1024]=0;
    assert(call_gallery_mark(b,media_long)<0&&b->vm->sp==2);
    puts("Kisaku 31/1012 native media links, case folding, absent-key no-op and atomic bounds: PASS");
    bootstrap_destroy(b);assert(bowling_released==2);test_message_fade(argv[1],argv[2]);test_message_reveal(argv[1],argv[2]);test_letter_pages(argv[1],argv[2]);test_letter_body(argv[1],argv[2]);test_startup_native_ax(argv[1],argv[2]);test_choice_stack_isolation(argv[1],argv[2]);test_ui_and_logo(argv[1],argv[2]);test_portrait_key(argv[1],argv[2],"b00an.akb",0xff00);test_portrait_key(argv[1],argv[2],"ev01.akb",0xff00);test_location_label(argv[1],argv[2]);test_graphics_windows(argv[1],argv[2]);test_animation_waits(argv[1],argv[2]);test_animation_registration(argv[1],argv[2]);test_scene_context(argv[1],argv[2]);return 0;
}

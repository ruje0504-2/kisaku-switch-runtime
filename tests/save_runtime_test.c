#include "bootstrap.h"
#include "save_slot.h"
#include "restore_name.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "backlog_store.h"

static void title(KBootstrap *b){
    for(unsigned i=0;i<3000;i++){
        assert(bootstrap_run(b,100000)>=0);
        if(b->title.active&&b->title.age>=64)return;
        bootstrap_frame(b);
    }
    assert(!"title timeout");
}
static void dialogue(KBootstrap *b){
    for(unsigned i=0;i<10000;i++){
        int result=bootstrap_run(b,100000);
        if(result<0){fprintf(stderr,"restore: %s\n",b->error);abort();}
        if(b->flag_dialog.active)bootstrap_pointer(b,300,350,1);
        if(b->message_active&&!b->message_slide&&!b->message_revealing&&!b->history_restore)return;
        bootstrap_frame(b);
    }
    assert(!"dialogue timeout");
}
static void same_backlog(const KBootstrap *a,const KBootstrap *b){
    assert(a->message_count==b->message_count&&a->message_index==b->message_index);
    assert((a->vm->globals[0][50].number&0x100)==(b->vm->globals[0][50].number&0x100));
    for(unsigned i=0;i<a->message_count;i++){
        const KMessageRecord *x=&a->messages[i],*y=&b->messages[i];
        assert(x->size==y->size&&x->flag==y->flag);
        if(x->size)assert(!memcmp(x->data,y->data,x->size));
        assert(!strcmp(x->text?x->text:"",y->text?y->text:""));
    }
}
static void put_number(uint8_t *p,size_t *n,int v){p[(*n)++]=0x32;for(int i=3;i>=0;i--)p[(*n)++]=(uint8_t)((unsigned)v>>(i*8));}
static void backlog_save_fixture(KBootstrap *b,const char *game,const char *root){
    KMessageRecord *old=b->messages;unsigned old_count=b->message_count;int old_index=b->message_index,flags=b->vm->globals[0][50].number;
    b->messages=calloc(72,sizeof(*b->messages));assert(b->messages);b->message_count=72;b->message_index=69;
    b->vm->globals[0][50].number|=0x100;
    uint8_t code[1024];size_t n=0;
    for(unsigned i=0;i<2;i++){
        put_number(code,&n,0);code[n++]=0x33;
        const char *name=b->voice.entries[i].name;size_t len=strlen(name)+1;memcpy(code+n,name,len);n+=len;
        put_number(code,&n,5);put_number(code,&n,17);code[n++]=0x18;
    }
    code[n++]=0x0a;code[n++]='A';code[n++]=0;code[n++]=0;
    for(unsigned i=0;i<70;i++){
        KMessageRecord *m=&b->messages[i];m->data=malloc(n);assert(m->data);memcpy(m->data,code,n);m->size=m->capacity=n;m->flag=1;
    }
    KControlRecord packed={0};KBacklogSnapshot *snapshot=NULL;
    assert(!kbacklog_store_pack(b,&packed)&&!kbacklog_store_unpack(&packed,&snapshot));kbacklog_snapshot_free(snapshot);snapshot=NULL;
    unsigned at=4+2*b->history_count;char *hex=(char *)packed.values[at].string;char keep=hex[0];hex[0]='z';
    assert(kbacklog_store_unpack(&packed,&snapshot)<0&&!snapshot);hex[0]=keep;
    packed.values[2].number=72;assert(kbacklog_store_unpack(&packed,&snapshot)<0&&!snapshot);packed.values[2].number=69;
    assert(!bootstrap_save_slot(b,12));
    KBootstrap *loaded=bootstrap_create_split(game,root);assert(loaded);title(loaded);
    assert(!bootstrap_load_slot(loaded,0,12));dialogue(loaded);same_backlog(b,loaded);
    assert(bootstrap_backlog_native(loaded)&&bootstrap_backlog_count(loaded)==70);
    KBacklogVoices voices={0};char why[256];assert(!bootstrap_backlog_replay(loaded,69,NULL,&voices,why));
    assert(voices.count==2&&!strcmp(voices.names[0],b->voice.entries[0].name)&&!strcmp(voices.names[1],b->voice.entries[1].name));kbacklog_voices_free(&voices);
    bootstrap_destroy(loaded);
    /* A valid outer save with a malformed v2 record must leave title/VM intact. */
    KFlags *f=NULL;KControlStore *controls=NULL;assert(!kslot_read(root,0,12,&f,&controls));
    KControlRecord *history=NULL;for(unsigned i=0;i<controls->count;i++)if(controls->records[i].type==0xfffd)history=&controls->records[i];assert(history);
    ((char *)history->values[at].string)[0]='z';assert(!kslot_write_state(root,0,13,f,controls->records,controls->count,&b->layers[0],&b->layers[4]));
    loaded=bootstrap_create_split(game,root);assert(loaded);title(loaded);uint8_t *raw=loaded->raw_variables;KMessageRecord *before=loaded->messages;
    assert(bootstrap_load_slot(loaded,0,13)<0&&loaded->title.active&&loaded->raw_variables==raw&&loaded->messages==before&&!loaded->backlog_restore);bootstrap_destroy(loaded);
    /* Version 1 retains its flattened text compatibility path. */
    for(unsigned i=0;i<history->count;i++)free((void *)history->values[i].string);free(history->values);
    history->count=3;history->values=calloc(3,sizeof(KValue));assert(history->values);history->values[0].number=1;
    char *text=malloc(4),*voice=malloc(1);assert(text&&voice);memcpy(text,"old",4);voice[0]=0;history->values[1].string=text;history->values[2].string=voice;
    assert(!kslot_write_state(root,0,14,f,controls->records,controls->count,&b->layers[0],&b->layers[4]));
    loaded=bootstrap_create_split(game,root);assert(loaded);title(loaded);assert(!bootstrap_load_slot(loaded,0,14));dialogue(loaded);
    assert(loaded->history_count==1&&!strcmp(loaded->history[0],"old"));bootstrap_destroy(loaded);
    kflags_free(f);kcontrol_free(controls);kbacklog_store_free(&packed);
    for(unsigned i=0;i<b->message_count;i++){free(b->messages[i].data);free(b->messages[i].text);}free(b->messages);
    b->messages=old;b->message_count=old_count;b->message_index=old_index;b->vm->globals[0][50].number=flags;
    puts("Native backlog save: 70 records, ordered voices, empty slots, recording state, corrupt data and v1 compatibility: PASS");
}
int main(int argc,char **argv){
    assert(argc==3);
    char name[261];assert(!krestore_name(name,"ev_m.adv")&&!strcmp(name,"EV_M.MES"));
    assert(!krestore_name(name,"ev01.mes")&&!strcmp(name,"EV01.MES"));
    KBootstrap *b=bootstrap_create_split(argv[1],argv[2]);assert(b);title(b);
    bootstrap_title_move(b,1);bootstrap_confirm(b);dialogue(b);
    for(unsigned slot=1;slot<=3;slot++){
        assert(bootstrap_can_save(b));
        int read=b->message_read_id,checkpoint=b->vm->globals[0][48].number;
        fprintf(stderr,"save slot=%u checkpoint=%d byte1001=%u\n",slot,checkpoint,b->vm->bytes[1001]);
        assert(!bootstrap_save_slot_comment(b,slot,"保存テスト"));
        assert(b->vm->raw==b->raw_variables&&b->vm->raw_size==b->raw_size);
        KFlags *f=NULL;KControlStore *controls=NULL;assert(!kslot_read(argv[2],0,slot,&f,&controls));
        KSlotInfo info;assert(!kslot_info(f,slot,&info)&&info.saved_time&&info.comment[0]);
        assert(f->byte_count==9192&&f->word_count==600&&f->raw_count==15000);
        assert(f->raw[1000]==slot-1&&f->raw[1001]==0);
        kflags_free(f);kcontrol_free(controls);
        KBootstrap *loaded=bootstrap_create_split(argv[1],argv[2]);assert(loaded);title(loaded);
        assert(loaded->title.native_ids[1]==1&&loaded->title.native_ids[3]==3);
        assert(!bootstrap_load_slot(loaded,0,slot));dialogue(loaded);
        fprintf(stderr,"loaded checkpoint=%d read=%d (expected %d)\n",loaded->vm->globals[0][48].number,loaded->message_read_id,read);
        same_backlog(b,loaded);
        assert(loaded->vm->globals[0][48].number==checkpoint&&loaded->message_read_id==read);
        assert(loaded->layers[0].width==b->layers[0].width&&loaded->layers[0].height==b->layers[0].height);
        unsigned diffs=0,minx=640,miny=480,maxx=0,maxy=0;
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)if(memcmp(loaded->layers[0].pixels+y*loaded->layers[0].stride+x*4,b->layers[0].pixels+y*b->layers[0].stride+x*4,3)){diffs++;if(x<minx)minx=x;if(y<miny)miny=y;if(x>maxx)maxx=x;if(y>maxy)maxy=y;}
        fprintf(stderr,"pixel differences=%u bounds=%u,%u..%u,%u bg=%s / %s flags=%x / %x\n",diffs,minx,miny,maxx,maxy,loaded->image_name,b->image_name,loaded->vm->globals[0][50].number,b->vm->globals[0][50].number);
        assert(!diffs);
        bootstrap_confirm(loaded);dialogue(loaded);
        bootstrap_confirm(b);dialogue(b);
        assert(loaded->message_read_id==b->message_read_id);same_backlog(b,loaded);
        bootstrap_destroy(loaded);
    }
    backlog_save_fixture(b,argv[1],argv[2]);
    /* Reach a real checkpoint after the first parameter animation. This
       catches private-window state that opening-only snapshots never touch. */
    for(unsigned i=0;b->param_values[0]!=805||!bootstrap_can_save(b)||b->message_revealing||b->param_animation_active;i++){
        assert(i<20000&&!b->title.active);
        if(b->message_read_id==27743&&b->exec526_active&&b->message_visible&&getenv("KISAKU_TEST_DUMP")){
            FILE *f=fopen("local/company-front.ppm","wb");assert(f);fprintf(f,"P6\n640 480\n255\n");
            for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){const uint8_t *p=b->layers[0].pixels+y*2560+x*4;uint8_t rgb[]={p[2],p[1],p[0]};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
        }
        if(b->choice_active){
            unsigned selected=0;
            while(selected<b->choice_count&&b->vm->bytes[2000+b->choice_values[selected]])selected++;
            if(selected==b->choice_count)selected=(unsigned)(b->frames%b->choice_count);
            b->choice_selected=(int)selected;bootstrap_confirm(b);
        }else if(b->message_active&&!b->message_slide)bootstrap_confirm(b);
        bootstrap_frame(b);int result=bootstrap_run(b,100000);
        if(result<0){fprintf(stderr,"later save probe: %s\n",b->error);abort();}
    }
    assert(!bootstrap_save_slot(b,11));
    KBootstrap *later=bootstrap_create_split(argv[1],argv[2]);assert(later);title(later);
    assert(!bootstrap_load_slot(later,0,11));dialogue(later);
    fprintf(stderr,"later checkpoint=%d param=%d/%d, words=%u/%u, read=%d/%d\n",b->vm->globals[0][48].number,
        b->param_values[0],later->param_values[0],b->vm->words[500],later->vm->words[500],b->message_read_id,later->message_read_id);
    assert(later->message_read_id==b->message_read_id&&!memcmp(later->param_values,b->param_values,sizeof(b->param_values)));
    unsigned later_diffs=0,lx=640,ly=480,rx=0,ry=0;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)if(memcmp(later->layers[0].pixels+y*later->layers[0].stride+x*4,b->layers[0].pixels+y*b->layers[0].stride+x*4,3)){
        later_diffs++;if(x<lx)lx=x;if(x>rx)rx=x;if(y<ly)ly=y;if(y>ry)ry=y;
    }
    fprintf(stderr,"later image diff=%u bounds=%u,%u..%u,%u location=%u/%u message=%u/%u flags=%x/%x bg=%s/%s\n",
        later_diffs,lx,ly,rx,ry,b->exec526_active,later->exec526_active,b->message_visible,later->message_visible,
        b->vm->globals[0][50].number,later->vm->globals[0][50].number,b->image_name,later->image_name);
    if(getenv("KISAKU_TEST_DUMP"))for(unsigned i=0;i<2;i++){
        const KImage *im=i?&later->layers[0]:&b->layers[0];FILE *f=fopen(i?"local/load-later.ppm":"local/save-later.ppm","wb");assert(f);
        fprintf(f,"P6\n640 480\n255\n");
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){const uint8_t *p=im->pixels+y*im->stride+x*4;uint8_t rgb[]={p[2],p[1],p[0]};assert(fwrite(rgb,1,3,f)==3);}
        assert(!fclose(f));
    }
    assert(!later_diffs);
    bootstrap_destroy(later);
    /* Version 5 must not load over a missing/truncated resident scene. */
    char index_path[4096],scene_path[4096];snprintf(index_path,sizeof(index_path),"%s/kisaku-slot-0-011.index",argv[2]);
    FILE *index=fopen(index_path,"rb");assert(index);unsigned version,generation;
    assert(fscanf(index,"KISAKU-SLOT-%u %u",&version,&generation)==2&&version==5);assert(!fclose(index));
    snprintf(scene_path,sizeof(scene_path),"%s/kisaku-slot-0-011-%u.scene",argv[2],generation);
    FILE *broken=fopen(scene_path,"wb");assert(broken);assert(fwrite("KASCENE1",1,8,broken)==8);assert(!fclose(broken));
    KBootstrap *rejected=bootstrap_create_split(argv[1],argv[2]);assert(rejected);title(rejected);
    uint8_t scene_pixel=rejected->layers[4].pixels[0];int title_mode=rejected->vm->globals[0][18].number;
    assert(bootstrap_load_slot(rejected,0,11)<0&&rejected->title.active&&rejected->layers[4].pixels[0]==scene_pixel&&rejected->vm->globals[0][18].number==title_mode);
    bootstrap_destroy(rejected);

    /* Script 25/3 flushes read history only, unlike a user save/load/quit. */
    KFlags *catalog_before=kflags_read_slot(argv[2],0,100);assert(catalog_before);
    int old_status=b->vm->status,old_syscall=b->vm->syscall;unsigned old_sp=b->vm->sp;
    uint8_t old_progress=b->vm->bytes[2500];b->vm->bytes[2500]=255;
    b->vm->status=KVM_SYSCALL;b->vm->syscall=25;assert(!kvm_push(b->vm,(KValue){3,NULL}));
    assert(!bootstrap_dispatch(b)&&b->vm->sp==old_sp);
    b->vm->status=old_status;b->vm->syscall=old_syscall;b->vm->bytes[2500]=old_progress;
    KFlags *catalog_after=kflags_read_slot(argv[2],0,100);assert(catalog_after);
    assert(!memcmp(catalog_before->bytes,catalog_after->bytes,9192));kflags_free(catalog_before);kflags_free(catalog_after);
    /* A deterministic later-state fixture carried through the real save and
       native restore preamble, independent of arbitrary route choices. */
    b->vm->bytes[1234]=17;b->vm->bytes[2500]=3;b->vm->bytes[4092]=41;
    b->vm->bytes[4007]=b->vm->bytes[4010]=b->vm->bytes[4011]=1;
    b->vm->words[541]=4;b->vm->words[500]=321;b->vm->words[501]=654;b->vm->words[502]=987;b->vm->words[503]=79;
    b->vm->words[552]=8;b->vm->words[553]=6;b->vm->words[99]=123;
    for(unsigned i=0;i<96;i++){b->vm->words[200+i]=i<8?(uint16_t)(1+i%6):0;b->vm->words[300+i]=i<8?(uint16_t)(1+i*3):0;}
    assert(!bootstrap_save_slot(b,10));
    b->vm->bytes[1234]=29;b->vm->bytes[2500]=9;b->vm->bytes[4092]=42;
    assert(!bootstrap_flush_progress(b));
    KBootstrap *restored=bootstrap_create_split(argv[1],argv[2]);assert(restored);title(restored);
    assert(!bootstrap_load_slot(restored,0,10));
    assert(restored->vm->bytes[1234]==17&&restored->vm->bytes[2500]==9&&restored->vm->bytes[4092]==41);
    assert(!restored->vm->bytes[4007]&&!restored->vm->bytes[4010]&&!restored->vm->bytes[4011]&&!restored->vm->words[99]);
    assert(restored->param_rows==4&&restored->param_values[0]==321&&restored->param_values[1]==654&&restored->param_values[2]==987&&restored->param_values[3]==79);
    assert(restored->param_markers[0]==8&&restored->param_markers[1]==6&&restored->param_surface.pixels);
    assert(restored->diary_days==2&&restored->diary_content_height==176&&restored->diary_surface.pixels);
    for(unsigned i=0;i<96;i++)assert(restored->diary_people[i]==b->vm->words[200+i]&&restored->diary_events[i]==b->vm->words[300+i]);
    dialogue(restored);assert(restored->message_read_id==b->message_read_id);
    bootstrap_destroy(restored);
    /* Synthetic alternate-mode snapshot exercises its distinct catalog/range;
       this is not evidence of alternate-story gameplay. */
    b->vm->globals[1][61].number=1;b->vm->bytes[4092]=51;b->vm->bytes[2500]=12;
    assert(!bootstrap_save_slot(b,10));b->vm->bytes[4092]=52;b->vm->bytes[2500]=13;
    assert(!bootstrap_flush_progress(b));
    restored=bootstrap_create_split(argv[1],argv[2]);assert(restored);title(restored);
    assert(!bootstrap_load_slot(restored,1,10)&&restored->vm->bytes[4092]==52&&restored->vm->bytes[2500]==13);
    bootstrap_destroy(restored);b->vm->globals[1][61].number=0;
    KBootstrap *unchanged=bootstrap_create_split(argv[1],argv[2]);assert(unchanged);title(unchanged);
    unsigned old_value=unchanged->vm->bytes[2500];uint8_t *old_surface=unchanged->param_surface.pixels;
    /* A corrupt catalog is never replaced with defaults or ignored on load. */
    char path[4096];snprintf(path,sizeof(path),"%s/kisaku-flag-0-100.dat",argv[2]);
    FILE *damaged=fopen(path,"wb");assert(damaged);assert(fwrite("broken",1,6,damaged)==6&&!fclose(damaged));
    assert(bootstrap_save_slot(b,10)<0);
    assert(bootstrap_load_slot(unchanged,0,10)<0&&unchanged->title.active&&unchanged->vm->bytes[2500]==old_value&&unchanged->param_surface.pixels==old_surface);
    bootstrap_destroy(unchanged);
    KFlags *saved=NULL;KControlStore *controls=NULL;assert(!kslot_read(argv[2],0,10,&saved,&controls));
    assert(saved->bytes[2500]==3);kflags_free(saved);kcontrol_free(controls);
    bootstrap_destroy(b);puts("Japanese save/checkpoint round trip, catalog merge, parameter/diary restoration and corrupt-catalog retention passed");return 0;
}

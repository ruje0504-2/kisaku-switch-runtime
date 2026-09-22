#include "bootstrap.h"
#include "save_slot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void title(KBootstrap *b){
    for(unsigned i=0;i<3000;i++){assert(bootstrap_run(b,100000)>=0);if(b->title.active&&b->title.age>=64)return;bootstrap_frame(b);}
    assert(!"title timeout");
}
static void dialogue(KBootstrap *b){
    for(unsigned i=0;i<10000;i++){
        int rc=bootstrap_run(b,100000);if(rc<0){fprintf(stderr,"letter restore: %s\n",b->error);abort();}
        if(b->flag_dialog.active)bootstrap_pointer(b,300,350,1);
        if(b->message_active&&!b->message_slide&&!b->message_revealing&&!b->history_restore&&!b->restore_pending)return;
        bootstrap_frame(b);
    }
    assert(!"dialogue timeout");
}
static void compare(KBootstrap *a,KBootstrap *b){
    assert(a->message_read_id==b->message_read_id&&a->letter_active==b->letter_active);
    assert(a->vm->globals[0][48].number==b->vm->globals[0][48].number);
    assert(a->vm->globals[0][46].number==b->vm->globals[0][46].number&&a->vm->globals[0][47].number==b->vm->globals[0][47].number);
    unsigned diffs=0;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)if(memcmp(a->layers[0].pixels+y*a->layers[0].stride+x*4,b->layers[0].pixels+y*b->layers[0].stride+x*4,3))diffs++;
    fprintf(stderr,"letter read=%d checkpoint=%d RGB differences=%u\n",a->message_read_id,a->vm->globals[0][48].number,diffs);assert(!diffs);
    if(a->letter_active){
        const KImage *oa=NULL,*ob=NULL;
        const KImage *ca=bootstrap_present_layers(a,&oa),*cb=bootstrap_present_layers(b,&ob);
        assert(oa&&ob&&ca!=&a->layers[0]&&cb!=&b->layers[0]);
        assert(!memcmp(oa->pixels,ob->pixels,960*720*4));
        unsigned ink=0;for(unsigned i=0;i<960*720;i++)ink+=oa->pixels[i*4+3]!=0;
        assert(ink>100);
        for(unsigned i=0;i<640*480;i++)assert(!memcmp(ca->pixels+i*4,cb->pixels+i*4,3));
    }
    assert(a->history_count==b->history_count);
    for(unsigned i=0;i<a->history_count;i++){
        unsigned ai=(a->history_next+63-i)%64,bi=(b->history_next+63-i)%64;
        assert(!strcmp(a->history[ai],b->history[bi])&&!strcmp(a->history_voice[ai],b->history_voice[bi]));
    }
}
int main(int argc,char **argv){
    assert(argc==3);KBootstrap *b=bootstrap_create_split(argv[1],argv[2]);assert(b);b->present_hires=1;title(b);
    bootstrap_title_move(b,1);bootstrap_confirm(b);dialogue(b);bootstrap_confirm(b);
    /* Use an unmodified original script with its real checkpoint table,
       library calls, page clearing and native load preamble. This fixture
       enters memo directly; it does not claim a full story route to it. */
    uint8_t *data=NULL;size_t size=0;assert(!ai6_read_named(&b->scripts,"memo.mes",&data,&size));
    int id=kvm_add_module(b->vm,"memo.mes",data,size);assert(id>=0);b->module_data[id]=data;
    b->vm->bytes[519]=0;assert(!kvm_start(b->vm,id));dialogue(b);
    assert(b->letter_active&&b->message_read_id==22208);
    KBootstrap *last_loaded=NULL;
    for(unsigned prompt=0;prompt<4;prompt++){
        assert(bootstrap_can_save(b));
        unsigned slot=21+prompt;assert(!bootstrap_save_slot(b,slot));
        KFlags *f=NULL;KControlStore *s=NULL;assert(!kslot_read(argv[2],0,slot,&f,&s));
        unsigned found=0;for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xfffe){
            assert(s->records[i].count==3&&s->records[i].values[0].number==1&&s->records[i].values[1].number==b->message_read_id&&s->records[i].values[2].number==2);found++;
        }assert(found==1);kflags_free(f);kcontrol_free(s);
        KBootstrap *loaded=bootstrap_create_split(argv[1],argv[2]);assert(loaded);loaded->present_hires=1;title(loaded);
        assert(!bootstrap_load_slot(loaded,0,slot));dialogue(loaded);compare(b,loaded);
        bootstrap_confirm(b);bootstrap_confirm(loaded);dialogue(b);dialogue(loaded);compare(b,loaded);
        if(prompt==3)last_loaded=loaded;else bootstrap_destroy(loaded);
    }
    bootstrap_message_hide(b,1);assert(!bootstrap_can_save(b)&&bootstrap_save_slot(b,25)<0);
    while(b->letter_transition)bootstrap_frame(b);assert(!bootstrap_can_save(b));
    bootstrap_message_hide(b,0);while(b->letter_transition)bootstrap_frame(b);assert(bootstrap_can_save(b));
    int read=b->message_read_id;b->message_read_id=-1;assert(!bootstrap_can_save(b));b->message_read_id=read;
    /* Corrupt mode metadata is rejected before replacing the fresh title. */
    KFlags *f=NULL;KControlStore *s=NULL;assert(!kslot_read(argv[2],0,21,&f,&s));
    for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xfffe)s->records[i].values[2].number=99;
    assert(!kslot_write_state(argv[2],0,25,f,s->records,s->count,&b->layers[0],&b->layers[4]));
    KBootstrap *bad=bootstrap_create_split(argv[1],argv[2]);assert(bad);title(bad);
    int module=bad->vm->module;uint8_t pixel=bad->layers[0].pixels[0];
    assert(bootstrap_load_slot(bad,0,25)<0&&bad->title.active&&bad->vm->module==module&&bad->layers[0].pixels[0]==pixel);
    bootstrap_destroy(bad);kflags_free(f);kcontrol_free(s);
    /* Both paths leave the final paragraph through the unmodified script. */
    bootstrap_confirm(b);bootstrap_confirm(last_loaded);dialogue(b);dialogue(last_loaded);
    assert(!b->letter_mode&&!last_loaded->letter_mode);compare(b,last_loaded);bootstrap_destroy(last_loaded);
    bootstrap_destroy(b);puts("Kisaku letter save: actual memo.mes paragraphs, page reconstruction, 960x720 glyph equality, RGB/history/next prompt and unsafe-state rejection: PASS");return 0;
}

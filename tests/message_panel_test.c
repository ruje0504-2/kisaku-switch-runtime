/* Exercise the actual frontend modal handlers, including finger ownership. */
#define main kisaku_viewer_main
#include "../tools/runtime_viewer.c"
#undef main
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
static void test_native_quit(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){77,NULL})&&!kvm_push(b->vm,(KValue){3,NULL}));
    assert(!bootstrap_dispatch(b)&&b->quit_modal&&b->message_request==6);
    assert(b->vm->sp==1&&b->vm->stack[0].number==77);
    unsigned frames=b->frames,events=b->input_events;
    bootstrap_frame(b);bootstrap_confirm(b);bootstrap_cancel(b);
    assert(bootstrap_run(b,1)==1&&b->frames==frames&&b->input_events==events);
    assert(!bootstrap_can_save(b));
    MessagePanel p={.kind=6};b->message_request=0;
    message_panel_action(&p,b,0);assert(p.kind==6&&b->quit_modal);
    message_panel_action(&p,b,5);message_panel_action(&p,b,0);
    assert(!p.kind&&!b->quit_modal&&!b->quit_requested&&b->vm->sp==1);
    for(unsigned cancel=0;cancel<2;cancel++){
        b->vm->status=KVM_SYSCALL;assert(!kvm_push(b->vm,(KValue){3,NULL}));
        assert(!bootstrap_dispatch(b));p.kind=6;p.viewing=0;
        if(!cancel){message_panel_action(&p,b,1);assert(!p.kind&&!b->quit_modal&&!b->message_request);}
        else{
            b->reset_pending=1;message_panel_action(&p,b,4);message_panel_action(&p,b,0);
            assert(p.kind==6&&b->quit_modal&&!b->quit_requested&&p.status[0]);
            b->reset_pending=0;message_panel_action(&p,b,0);
            assert(!p.kind&&!b->quit_modal&&b->quit_requested&&b->vm->sp==1);
            assert(bootstrap_run(b,1)==1);
        }
    }
    bootstrap_destroy(b);
    puts("Kisaku 31/3 exit dialog: suspended VM, no return value, No/cancel, Yes and flush failure: PASS");
}
static void test_name_editor(const char *root,const char *saves,SDL_Renderer *renderer){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    /* Exercise the real 31/810 dispatch before driving the frontend.  The
       old test only toggled extra_active by hand and could not catch a VM
       that stayed suspended after the name modal closed. */
    b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;
    assert(!kvm_push(b->vm,(KValue){810,NULL}));
    assert(!bootstrap_dispatch(b)&&b->extra_active&&b->extra_kind==14&&b->vm->status==KVM_READY);
    MessagePanel p={.kind=14,.name_focus=0};snprintf(p.name,sizeof(p.name),"鬼作");p.name_text_cursor=1;
    assert(!name_insert_text(&p,"郎")&&!strcmp(p.name,"鬼郎作")&&p.name_text_cursor==2);
    name_delete_at(&p);assert(!strcmp(p.name,"鬼作")&&p.name_text_cursor==1);
    assert(name_insert_text(&p,"一二三四")<0&&!strcmp(p.name,"鬼作"));
    message_panel_draw(&p,b,renderer);assert(p.name_artwork.pixels&&p.name_artwork.width==640&&p.name_artwork.height==300);
    /* The PC kanji bar has ten fixed jump points and one-step arrows.  The
       final jump is 184; the renderer must blank cells past the recovered
       186-row table instead of indexing beyond it. */
    p.name_mode=1;name_panel_pointer(&p,b,116,336,1);assert(p.name_page==14);
    name_panel_pointer(&p,b,512,336,1);assert(p.name_page==15);
    name_panel_pointer(&p,b,460,336,1);assert(p.name_page==14);
    name_panel_pointer(&p,b,404,336,1);assert(p.name_page==184);
    message_panel_draw(&p,b,renderer);assert(!b->error[0]);
    /* Backspace is the atlas button at x=432, while the two arrows move the
       text cursor even though the grid currently has focus. */
    p.name_mode=0;p.name_focus=0;p.name_text_cursor=2;
    name_panel_pointer(&p,b,432,380,1);assert(!strcmp(p.name,"鬼"));
    snprintf(p.name,sizeof(p.name),"鬼作");p.name_text_cursor=0;
    message_panel_action(&p,b,2);assert(p.name_focus==1);
    p.name_cursor=0;message_panel_action(&p,b,0);assert(name_utf8_count(p.name)==3);
    p.name_focus=0;message_panel_action(&p,b,0);assert(p.name_confirm&&p.selected==0);
    message_panel_action(&p,b,0);assert(!p.kind&&!b->extra_active&&b->vm->bytes[1950]!=0);
    assert(!b->error[0]&&b->vm->status==KVM_READY);
    SDL_DestroyTexture(p.texture);rmt_free(&p.image);rmt_free(&p.name_artwork);bootstrap_destroy(b);
    puts("Kisaku CName modal: namepart atlas, 18x12 CP932 grid, five-character limit and confirm flow: PASS");
}
static void test_appendix_media(const char *root,const char *saves,SDL_Renderer *renderer){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    MessagePanel p={.kind=9,.selected=0};
    b->vm->bytes[3260]=1; /* one unlocked track is enough to exercise state 0/1 */
    message_panel_draw(&p,b,renderer);
    assert(p.appendix_artwork.pixels&&p.appendix_artwork.width==714&&p.appendix_artwork.height==540);
    /* music_mode.area stop and return buttons are kept in their native 640x480 coordinates. */
    b->extra_active=1;b->extra_kind=b->extra_request=9;b->music_active=1;
    panel_touch(&p,b,NULL,450,460);assert(!b->music_active&&b->extra_active);
    b->vm->sp=0;panel_touch(&p,b,NULL,580,460);assert(!p.kind&&!b->extra_active&&b->vm->sp==1&&b->vm->stack[0].number==0);
    p.kind=23;p.selected=0;p.status[0]=0;b->vm->sp=0;b->vm->bytes[3600]=1;b->extra_active=1;b->extra_kind=b->extra_request=23;
    message_panel_draw(&p,b,renderer);
    assert(p.appendix_artwork.pixels&&p.appendix_artwork.width==640&&p.appendix_parts.pixels&&p.appendix_parts.width==585);
    /* videomode.area slot 0 is (34,8)-(49,123). */
    panel_touch(&p,b,NULL,35,9);assert(!p.kind&&!b->extra_active&&b->vm->sp==1&&b->vm->stack[0].number==0);
    rmt_free(&p.image);rmt_free(&p.appendix_artwork);rmt_free(&p.appendix_parts);SDL_DestroyTexture(p.texture);bootstrap_destroy(b);
    puts("Kisaku appendix media: music/video AKB atlases, native AREA hit boxes and selector returns: PASS");
}
static void test_saved_parameters(SaveMenu *m,KBootstrap *b,SDL_Renderer *r,const char *shot){
    KFlags *saved=m->states[0];assert(saved&&saved->word_count==600);
    uint16_t words[600];memcpy(words,saved->words,sizeof(words));
    uint64_t live_words=ui_hash(1,b->vm->words,b->vm->word_count*sizeof(*b->vm->words));
    uint64_t live_pixels=b->param_surface.pixels?ui_hash(1,b->param_surface.pixels,b->param_surface.height*b->param_surface.stride):0;
    saved->words[500]=805;saved->words[501]=200;saved->words[502]=300;saved->words[503]=42;
    saved->words[552]=2;saved->words[553]=3;saved->words[556]=12;saved->words[557]=8;
    KImage image={0},atlas={0};uint8_t *data=NULL;size_t size=0;
    assert(!ai6_read_named(&b->images,"param.akb",&data,&size)&&!akb_decode(data,size,&atlas));free(data);
    for(unsigned rows=1;rows<=4;rows++){
        saved->words[541]=(uint16_t)rows;assert(!bootstrap_saved_param_image(b,saved,&image));
        assert(image.width==602&&image.height==(rows==4?112:30+29*(rows-1)));
        for(unsigned c=0;c<3;c++)assert(image.pixels[11*image.stride+100*4+c]==atlas.pixels[112*atlas.stride+501*4+c]);
        if(rows==4){
            const unsigned positions[]={166,174,474,482,534,542},glyphs[]={4,2,1,2,11,8};
            for(unsigned i=0;i<6;i++)for(unsigned y=0;y<16;y++)assert(!memcmp(image.pixels+(92+y)*image.stride+positions[i]*4,atlas.pixels+(glyphs[i]<10?112+y:128+y)*atlas.stride+(524+(glyphs[i]%10)*8)*4,32));
        }
    }
    uint8_t *kept=image.pixels;uint64_t hash=ui_hash(1,image.pixels,image.height*image.stride);
    saved->words[552]=9;assert(bootstrap_saved_param_image(b,saved,&image)<0&&image.pixels==kept&&ui_hash(1,image.pixels,image.height*image.stride)==hash);saved->words[552]=2;
    saved->word_count=557;assert(bootstrap_saved_param_image(b,saved,&image)<0&&image.pixels==kept);saved->word_count=600;
    assert(!b->error[0]&&ui_hash(1,b->vm->words,b->vm->word_count*sizeof(*b->vm->words))==live_words);
    if(b->param_surface.pixels)assert(ui_hash(1,b->param_surface.pixels,b->param_surface.height*b->param_surface.stride)==live_pixels);
    m->draw_key=0;m->hover=-1;rmt_free(&m->previews[0]);save_menu_draw(m,b,r);
    assert(m->previews[0].height==112);
    for(unsigned y=0;y<49;y++)for(unsigned x=0;x<268;x++)assert(!memcmp(m->canvas.pixels+(272+y)*m->canvas.stride+(8+x)*4,image.pixels+(y*112/49)*image.stride+(x*602/268)*4,3));
    if(shot){char path[4096];snprintf(path,sizeof(path),"%s.save-parameters.bmp",shot);assert(!capture(r,path));}
    unsigned hit=save_menu_hit(m,30,280);assert(hit>>8==6);
    save_menu_pointer(m,b,30,280,1);assert(m->param_detail&&!m->overwrite&&!m->confirm_load);
    m->param_steps=0;save_menu_draw(m,b,r);
    for(unsigned y=0;y<112;y++)assert(!memcmp(m->canvas.pixels+(272+y)*m->canvas.stride+19*4,image.pixels+y*image.stride,602*4));
    if(shot){char path[4096];snprintf(path,sizeof(path),"%s.save-parameters-full.bmp",shot);assert(!capture(r,path));}
    int slot=m->slot;save_menu_action(m,b,9);assert(m->slot==slot&&m->param_detail);
    save_menu_action(m,b,1);assert(!m->param_detail&&m->active&&!m->overwrite);
    save_menu_action(m,b,7);assert(m->param_detail);m->param_steps=0;save_menu_action(m,b,0);assert(!m->param_detail&&!m->overwrite);
    m->selector=1;save_menu_action(m,b,7);assert(!m->param_detail);m->selector=0;
    unsigned old_detail=saved->bytes[108];saved->bytes[108]=1;m->draw_key=0;
    save_menu_pointer(m,b,30,80,1);assert(m->character_detail&&m->detail_index==0);
    m->detail_steps=0;save_menu_draw(m,b,r);assert(m->detail_artwork.pixels&&m->detail_status_artwork.pixels&&m->character_detail);
    assert(!capture(r,"/tmp/detail-current.bmp"));
    uint64_t detail_pixels=ui_hash(1,m->canvas.pixels,m->canvas.stride*m->canvas.height);
    m->draw_key=0;save_menu_draw(m,b,r);assert(ui_hash(1,m->canvas.pixels,m->canvas.stride*m->canvas.height)==detail_pixels);
    uint8_t *original=malloc(saved->byte_count);assert(original);memcpy(original,saved->bytes,saved->byte_count);
    memset(saved->bytes,0,saved->byte_count);saved->bytes[237]=1;saved->bytes[120]=23;
    m->draw_key=0;save_menu_draw(m,b,r);
    character_detail_pointer(m,b,40,65,0);assert(m->detail_hover==4);
    character_detail_pointer(m,b,40,97,0);assert(m->detail_hover==5);
    character_detail_pointer(m,b,40,129,0);assert(m->detail_hover==-1);
    character_detail_action(m,b,3);assert(m->detail_hover==4);
    character_detail_action(m,b,3);assert(m->detail_hover==5);
    character_detail_action(m,b,3);assert(m->detail_hover==1000);
    character_detail_action(m,b,2);assert(m->detail_hover==5);
    character_detail_pointer(m,b,40,65,1);assert(m->detail_view&&m->detail_image.pixels);
    m->detail_steps=0;save_menu_draw(m,b,r);
    character_detail_action(m,b,1);m->detail_steps=0;save_menu_draw(m,b,r);
    assert(m->character_detail&&!m->detail_view&&!m->detail_image.pixels);
    for(unsigned role=0;role<8;role++){
        memset(saved->bytes,0,saved->byte_count);m->detail_index=role;m->detail_hover=-1;
        rmt_free(&m->detail_status_artwork);
        for(unsigned item=0;item<character_counts[role];item++){
            /* Hiro entries 5 and 6 intentionally share native byte 237. */
            saved->bytes[character_flags[role][item]]=0;
            assert(!character_enabled(saved,role,item));
            saved->bytes[character_flags[role][item]]=2;assert(!character_enabled(saved,role,item));
            saved->bytes[character_flags[role][item]]=1;assert(character_enabled(saved,role,item));
            KImage cg={0};const char *name=bootstrap_character_image(role,item);
            assert(name&&!ui_asset(b,name,&cg));rmt_free(&cg);
        }
        m->draw_key=0;save_menu_draw(m,b,r);assert(!m->status[0]);
        unsigned last=character_counts[role]-1;
        character_detail_pointer(m,b,40,65+(int)last*32,0);assert(m->detail_hover==(int)last);
    }
    memset(saved->bytes,0,saved->byte_count);saved->bytes[635]=1;assert(character_enabled(saved,1,8));
    assert(!bootstrap_character_image(8,0)&&!bootstrap_character_image(0,9));
    /* CGirlStatus's current-state strip keeps several route-specific
       branches which are easy to lose when only the row unlock flags are
       ported.  Check the exact source page selected for the ordinary,
       special and final states, including Madoka/Hiro's extra flags. */
    memset(saved->bytes,0,saved->byte_count);
    assert(character_dynamic_source_y(saved,0)==560);
    saved->bytes[147]=1;assert(character_dynamic_source_y(saved,0)==560);
    saved->bytes[147]=0;saved->bytes[120]=1;assert(character_dynamic_source_y(saved,0)==592);
    saved->bytes[171]=1;assert(character_dynamic_source_y(saved,0)==624);
    saved->bytes[179]=1;assert(character_dynamic_source_y(saved,0)==592);
    memset(saved->bytes,0,saved->byte_count);saved->bytes[172]=1;assert(character_dynamic_source_y(saved,1)==656);
    saved->bytes[180]=1;assert(character_dynamic_source_y(saved,1)==592);
    memset(saved->bytes,0,saved->byte_count);saved->bytes[177]=1;assert(character_dynamic_source_y(saved,6)==624);
    saved->bytes[618]=1;assert(character_dynamic_source_y(saved,6)==592);
    memset(saved->bytes,0,saved->byte_count);saved->bytes[149]=1;saved->bytes[181]=1;
    assert(character_dynamic_source_y(saved,2)==560);
    memset(saved->bytes,0,saved->byte_count);saved->bytes[152]=1;saved->bytes[184]=1;
    assert(character_dynamic_source_y(saved,5)==560);
    /* Momo/Nadeshiko and Aoi use distinct final-state strips in statfr.akb;
       these are easy to miss when all roles are reduced to the common 0x250
       row.  Cross-check the native 0x2b0/0x290 source offsets. */
    memset(saved->bytes,0,saved->byte_count);saved->bytes[182]=1;
    assert(character_dynamic_source_y(saved,3)==688);
    memset(saved->bytes,0,saved->byte_count);saved->bytes[183]=1;
    assert(character_dynamic_source_y(saved,4)==688);
    memset(saved->bytes,0,saved->byte_count);saved->bytes[186]=1;
    assert(character_dynamic_source_y(saved,7)==656);
    memcpy(saved->bytes,original,saved->byte_count);free(original);
    m->detail_index=0;save_menu_action(m,b,1);m->detail_steps=0;character_detail_frame(m);
    assert(!m->character_detail&&m->active);
    saved->bytes[108]=(uint8_t)old_detail;m->draw_key=0;
    memcpy(saved->words,words,sizeof(words));rmt_free(&image);rmt_free(&atlas);rmt_free(&m->previews[0]);m->draw_key=0;
    rmt_free(&m->detail_artwork);rmt_free(&m->detail_status_artwork);
    puts("Kisaku saved parameter/character detail: selected-slot words, four clipped heights, exact pixels, modal input, unlock gating and untouched live state: PASS");
}
static void test_save_menu(const char *root,const char *saves,SDL_Renderer *r,const char *shot){
    char folder[4096];snprintf(folder,sizeof(folder),"%s/ui-save",saves);assert(!mkdir(folder,0700));
    KBootstrap *b=bootstrap_create_split(root,folder);assert(b&&!b->error[0]);
    for(unsigned i=0;!b->title.active||b->title.age<64;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
    bootstrap_title_move(b,1);bootstrap_confirm(b);
    for(unsigned i=0;!bootstrap_can_save(b)||b->message_slide||b->message_revealing;i++){
        assert(i<10000&&bootstrap_run(b,100000)>=0);
        if(b->flag_dialog.active)bootstrap_pointer(b,300,350,1);
        bootstrap_frame(b);
    }
    SaveMenu m={0};save_menu_open(&m,b,0);assert(m.active&&m.save&&m.slot==1);
    save_menu_draw(&m,b,r);assert(m.texture&&m.area_count==31&&!m.valid[0]);
    assert(save_menu_hit(&m,296,28)==256&&save_menu_hit(&m,639,387)==265);
    assert(save_menu_hit(&m,275,10)==513&&save_menu_hit(&m,639,479)==768);
    save_menu_action(&m,b,8);assert(m.slot==91);save_menu_action(&m,b,9);assert(m.slot==1);
    save_menu_pointer(&m,b,380,40,1);assert(m.overwrite&&m.button==2);
    save_menu_action(&m,b,0);assert(m.overwrite&&!m.valid[0]); /* No implicit Yes. */
    strcpy(m.note,"保存テスト");save_menu_draw(&m,b,r);assert(m.note_artwork.pixels);
    if(shot){char path[4096];snprintf(path,sizeof(path),"%s.save-note.bmp",shot);assert(!capture(r,path));}
    save_menu_pointer(&m,b,350,390,1);assert(!m.overwrite&&!m.valid[0]); /* No. */
    save_menu_action(&m,b,0);strcpy(m.note,"保存テスト");save_menu_action(&m,b,4);save_menu_action(&m,b,0);
    assert(!m.overwrite&&m.valid[0]&&m.states[0]&&m.info[0].comment[0]);
    test_saved_parameters(&m,b,r,shot);
    save_menu_draw(&m,b,r);
    if(shot){char path[4096];snprintf(path,sizeof(path),"%s.save-list.bmp",shot);assert(!capture(r,path));}
    save_menu_action(&m,b,6);assert(!m.save);save_menu_action(&m,b,0);assert(m.confirm_load&&!m.loading&&m.button==2);
    save_menu_action(&m,b,0);assert(m.confirm_load&&!m.loading);
    save_menu_draw(&m,b,r);assert(m.dialog_artwork.pixels);
    if(shot){char path[4096];snprintf(path,sizeof(path),"%s.load-confirm.bmp",shot);assert(!capture(r,path));}
    save_menu_pointer(&m,b,240,390,2);assert(!m.confirm_load&&m.active); /* Right click cancels. */
    save_menu_action(&m,b,0);save_menu_pointer(&m,b,240,390,1);assert(m.loading&&m.next);
    int read=b->message_read_id;
    for(unsigned i=0;m.loading;i++){assert(i<10000);save_menu_step(&m,&b);SDL_Delay(1);}
    assert(!m.active&&b->message_read_id==read&&!b->error[0]);
    save_menu_clear(&m);bootstrap_destroy(b);memset(&m,0,sizeof(m));
    /* Old portable saves have a valid checkpoint but no catalog enable bit. */
    KFlags *catalog=kflags_read_slot(folder,0,100);assert(catalog);
    catalog->globals[1][60]=(KValue){0,NULL};assert(!kflags_write_slot(catalog,folder,0,100));kflags_free(catalog);
    b=bootstrap_create_split(root,folder);assert(b);
    for(unsigned i=0;!b->title.active;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
    assert(b->title.native_ids[1]==1&&b->title.native_ids[3]==3);
    b->title.selected=3;bootstrap_confirm(b);
    for(unsigned i=0;!b->title_reset_modal;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
    MessagePanel reset={.kind=21};b->message_request=0;
    message_panel_draw(&reset,b,r);assert(reset.dialog_body.pixels);
    message_panel_action(&reset,b,1);assert(!reset.kind&&!b->title_reset_modal);
    rmt_free(&reset.image);rmt_free(&reset.dialog_body);rmt_free(&reset.dialog_artwork);SDL_DestroyTexture(reset.texture);
    for(unsigned i=0;!b->title.active;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
    for(unsigned attempt=0;attempt<2;attempt++){
        b->title.selected=1;bootstrap_confirm(b);
        for(unsigned i=0;!b->file_modal;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
        assert(b->message_request==3);b->message_request=0;
        save_menu_open(&m,b,1);save_menu_draw(&m,b,r);assert(m.active&&!m.save&&m.valid[0]);
        if(!attempt){
            save_menu_action(&m,b,1);assert(!m.active&&!b->file_modal);
            for(unsigned i=0;!b->title.active;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
        }else{
            save_menu_action(&m,b,0);save_menu_action(&m,b,4);save_menu_action(&m,b,0);assert(m.loading);
            for(unsigned i=0;m.loading;i++){assert(i<10000);save_menu_step(&m,&b);SDL_Delay(1);}
            assert(!m.active&&!b->file_modal&&b->message_read_id==read&&!b->error[0]);
        }
    }
    save_menu_clear(&m);bootstrap_destroy(b);
    DIR *dir=opendir(folder);assert(dir);struct dirent *entry;
    while((entry=readdir(dir)))if(strcmp(entry->d_name,".")&&strcmp(entry->d_name,"..")){
        char path[8192];snprintf(path,sizeof(path),"%s/%s",folder,entry->d_name);assert(!remove(path));
    }
    closedir(dir);assert(!rmdir(folder));
    puts("Kisaku native save UI: 100 slots, actual AREA/page overlap, note/Yes/No, controller/mouse, and confirmed asynchronous restore: PASS");
}
static void backlog_test_number(uint8_t *code,size_t *at,int value){
    code[(*at)++]=0x32;for(int i=3;i>=0;i--)code[(*at)++]=(uint8_t)((unsigned)value>>(i*8));
}
static void test_native_backlog(const char *root,const char *saves,SDL_Renderer *renderer){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(bootstrap_run(b,100000)>=0&&b->layers[0].pixels);
    for(unsigned i=0;i<b->message_count;i++){free(b->messages[i].data);free(b->messages[i].text);}free(b->messages);
    b->messages=calloc(1,sizeof(*b->messages));assert(b->messages);b->message_count=1;b->message_index=0;
    uint8_t code[256];size_t at=0;const char *names[]={"z09577.ogg","z09578.ogg"};
    for(unsigned i=0;i<2;i++){
        backlog_test_number(code,&at,0);code[at++]=0x33;memcpy(code+at,names[i],strlen(names[i])+1);at+=strlen(names[i])+1;
        backlog_test_number(code,&at,5);backlog_test_number(code,&at,17);code[at++]=0x18;
        code[at++]=0x0a;code[at++]='A'+(uint8_t)i;code[at++]=0;
    }
    code[at++]=0;b->messages[0].data=malloc(at);assert(b->messages[0].data);memcpy(b->messages[0].data,code,at);
    b->messages[0].size=b->messages[0].capacity=at;b->messages[0].flag=1;
    MessagePanel p={.kind=5};backlog_panel_draw(&p,b,renderer);assert(!p.status[0]&&backlog_row_back(&p,b,5)==0);
    backlog_pointer(&p,b,100,440,1);assert(p.voice_loading&&p.voice_sequence.count==2&&p.voice_next==1);
    int ready=0;for(unsigned i=0;i<1000&&!ready;i++){ready=kvoice_worker_poll(p.voice_worker,&p.voice_pcm,&p.voice_size);if(!ready)SDL_Delay(5);}
    assert(ready==1&&p.voice_pcm&&p.voice_size);p.voice_loading=0;p.voice_at=p.voice_size;
    /* A completed first PCM and empty output queue must advance to the
       second real archive voice, without restarting or touching gameplay. */
    assert(!SDL_InitSubSystem(SDL_INIT_AUDIO));SDL_AudioSpec spec={0};spec.freq=44100;spec.format=AUDIO_S16LSB;spec.channels=2;spec.samples=512;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);
    KHistoryAudio saved={0};unsigned serial=0;
    assert(!history_voice_audio(&p,&saved,device,44100,2,&serial));
    assert(p.voice_loading&&p.voice_next==2&&p.voice_sequence.count==2&&!b->voice_active&&!b->voice_loading);
    uint8_t *pcm=NULL;size_t bytes=0;ready=0;
    for(unsigned i=0;i<1000&&!ready;i++){ready=kvoice_worker_poll(p.voice_worker,&pcm,&bytes);if(!ready)SDL_Delay(5);}
    assert(ready==1&&pcm&&bytes);free(pcm);
    backlog_pointer(&p,b,0,0,2);assert(!p.voice_loading&&!p.voice_sequence.count&&!p.voice_runtime&&p.kind==5);
    backlog_close(&p,b);assert(!history_voice_audio(&p,&saved,device,44100,2,&serial)&&!saved.suspended);
    KMessageRecord *grown=realloc(b->messages,70*sizeof(*grown));assert(grown);b->messages=grown;
    for(unsigned i=1;i<70;i++){b->messages[i]=(KMessageRecord){0};b->messages[i].data=malloc(at);assert(b->messages[i].data);memcpy(b->messages[i].data,code,at);b->messages[i].size=b->messages[i].capacity=at;b->messages[i].flag=1;}
    b->message_count=70;b->history_count=64;p.kind=5;p.viewing=0;p.status[0]=0;
    backlog_panel_draw(&p,b,renderer);assert(bootstrap_backlog_count(b)==70&&backlog_limit(b)==64);
    backlog_drag(&p,b,0);assert(backlog_row_back(&p,b,0)==69);backlog_panel_draw(&p,b,renderer);assert(!p.status[0]);backlog_close(&p,b);
    SDL_CloseAudioDevice(device);history_voice_stop(&p);kvoice_worker_destroy(p.voice_worker);
    SDL_DestroyTexture(p.texture);rmt_free(&p.image);rmt_free(&p.history_artwork);bootstrap_destroy(b);
    puts("Native backlog frontend: command row, two actual voice decodes, ordered advance and cancellation isolation: PASS");
}
static void test_backlog(MessagePanel *p,KBootstrap *b,SDL_Renderer *r,const char *shot){
    p->kind=5;p->viewing=0;b->history_count=b->history_next=0;
    memset(b->history,0,sizeof(b->history));memset(b->history_voice,0,sizeof(b->history_voice));
    memset(b->layers[0].pixels,100,640*480*4);backlog_panel_draw(p,b,r);
    assert(p->history_artwork.pixels&&p->backlog_area_count==9&&!p->status[0]);
    assert(p->image.pixels[0]==24&&backlog_row_back(p,b,5)==-1&&!backlog_limit(b));
    for(unsigned i=0;i<9;i++){
        ConfigArea a=p->backlog_areas[i];assert(backlog_hit(p,a.x0,a.y0)==a.id&&backlog_hit(p,a.x1-1,a.y1-1)==a.id);
    }
    assert(backlog_hit(p,616,20)==-1&&backlog_hit(p,640,20)==-1&&backlog_hit(p,10,480)==-1);
    b->history_count=b->history_next=2;
    strcpy(b->history[0],"Older text without a voice.");
    /* CP932: テスト。 Plain Japanese fixture, independent of story content. */
    strcpy(b->history[1],"\x83\x65\x83\x58\x83\x67\x81\x42");strcpy(b->history_voice[1],"a0001.ogg");
    backlog_panel_draw(p,b,r);
    assert(backlog_row_back(p,b,3)==-1&&backlog_row_back(p,b,4)==1&&backlog_row_back(p,b,5)==0);
    unsigned pixels=0;for(int y=410;y<440;y++)for(int x=32;x<100;x++)pixels+=p->image.pixels[y*p->image.stride+x*4]>24;assert(pixels>20);
    /* Entire voiced row is clickable. Nonvoiced and empty rows do nothing. */
    backlog_pointer(p,b,100,100,1);assert(!p->voice_loading&&p->kind==5);
    backlog_pointer(p,b,100,350,1);assert(!p->voice_loading&&p->kind==5);
    backlog_pointer(p,b,580,440,1);assert(p->voice_loading&&p->back==0);
    uint8_t *pcm=NULL;size_t pcm_size=0;int ready=0;
    for(unsigned i=0;i<1000&&!ready;i++){ready=kvoice_worker_poll(p->voice_worker,&pcm,&pcm_size);if(!ready)SDL_Delay(5);}
    assert(ready==1&&pcm&&pcm_size);free(pcm);
    backlog_pointer(p,b,0,0,2);assert(p->kind==5&&!p->voice_loading); /* First cancel interrupts voice. */
    assert(bootstrap_message_setting(b,KSET_CHARACTER_VOICE+10,-1)==0);
    backlog_pointer(p,b,100,440,1);assert(!p->voice_loading&&p->status[0]);
    assert(bootstrap_message_setting(b,KSET_CHARACTER_VOICE+10,1)==1);p->status[0]=0;
    backlog_pointer(p,b,628,10,1);assert(!p->backlog_offset); /* No scroll for two records. */
    p->backlog_hover=-1;backlog_panel_action(p,b,2);assert(p->backlog_hover==257);
    backlog_panel_action(p,b,4);assert(p->backlog_hover==4);
    backlog_panel_action(p,b,5);assert(p->backlog_hover==257);
    if(shot){char path[4096];snprintf(path,sizeof(path),"%s.backlog-partial.bmp",shot);backlog_panel_draw(p,b,r);assert(!capture(r,path));}
    b->history_count=64;b->history_next=2;
    for(unsigned back=0;back<64;back++){
        unsigned index=(b->history_next+63-back)%64;snprintf(b->history[index],sizeof(b->history[index]),"History record %u\n\x83\x65\x83\x58\x83\x67\x81\x42",64-back);
        strcpy(b->history_voice[index],back%2?"":"a0001.ogg");
    }
    assert(backlog_limit(b)==58);backlog_pointer(p,b,628,10,1);assert(p->backlog_offset==1);
    assert(backlog_row_back(p,b,0)==6&&backlog_row_back(p,b,5)==1);
    backlog_panel_action(p,b,8);assert(p->backlog_offset==7);
    backlog_drag(p,b,-100);assert(p->backlog_offset==58&&backlog_row_back(p,b,0)==63);
    backlog_drag(p,b,240);assert(p->backlog_offset==29);
    backlog_drag(p,b,999);assert(!p->backlog_offset);
    /* A held scrollbar contact remains a drag even after it leaves the rail. */
    MenuTouch touch={0};SaveMenu menu={0};SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.fingerId=19;
    e.tfinger.x=(float)(160+628*1.5)/1280;e.tfinger.y=240.0f/480;
    assert(menu_touch_event(&touch,&menu,p,b,&e,0)&&touch.drag==100&&p->backlog_offset==29);
    e.type=SDL_FINGERMOTION;e.tfinger.x=0.5f;e.tfinger.y=0;
    assert(menu_touch_event(&touch,&menu,p,b,&e,100)&&p->backlog_offset==58);
    e.type=SDL_FINGERUP;e.tfinger.y=450.0f/480;
    assert(menu_touch_event(&touch,&menu,p,b,&e,200)&&p->kind==5&&!p->voice_loading&&!p->backlog_mouse_drag);
    backlog_pointer(p,b,100,100,0);backlog_panel_draw(p,b,r);assert(!p->status[0]);
    if(shot){char path[4096];snprintf(path,sizeof(path),"%s.backlog-full.bmp",shot);assert(!capture(r,path));}
    backlog_scroll(p,b,-100,0);assert(!p->backlog_offset&&p->kind==5);
    backlog_scroll(p,b,-1,1);assert(!p->kind&&p->dismiss_menus);
    p->kind=5;p->viewing=0;backlog_panel_draw(p,b,r);assert(!p->backlog_offset&&p->backlog_hover==-1);
    backlog_panel_action(p,b,1);assert(!p->kind);
    history_voice_stop(p);kvoice_worker_destroy(p->voice_worker);p->voice_worker=NULL;
    rmt_free(&p->history_artwork);b->history_count=b->history_next=0;
    puts("Kisaku backlog: native artwork/AREA, bottom-aligned history, single-entry scroll, wraparound, controller/touch, actual voice decode and character mute: PASS");
}
static void test_config_motion(const char *root,const char *saves,SDL_Renderer *r,const char *shot){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    assert(bootstrap_run(b,100000)>=0&&b->layers[0].pixels);
    memset(b->layers[0].pixels,100,480*b->layers[0].stride);
    MessagePanel p={.kind=8};unsigned original=(unsigned)bootstrap_message_setting(b,8,0);
    for(unsigned speed=0;speed<3;speed++){
        assert(bootstrap_message_setting(b,8,(int)speed-bootstrap_message_setting(b,8,0))==(int)speed);
        p.kind=8;p.setting_ready=0;settings_panel_init(&p,b);
        unsigned steps=speed==0?12:speed==1?6:0;
        assert(p.config_steps==steps&&p.config_motion==(steps?1:0));
        uint64_t start=p.config_clock;
        if(steps){
            settings_panel_draw_at(&p,b,r,start);
            for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)for(unsigned c=0;c<3;c++)assert(p.image.pixels[y*p.image.stride+x*4+c]==100);
            int red=p.setting_values[11];settings_panel_pointer(&p,b,616,110,1);settings_panel_action(&p,b,0);settings_panel_action(&p,b,1);
            assert(p.config_motion==1&&p.setting_values[11]==red&&p.kind==8);
            settings_panel_draw_at(&p,b,r,start+steps/2*15);
            assert(p.config_step==steps/2&&p.config_motion==1);
            assert(p.image.pixels[200*p.image.stride+200*4]==100*(255-192*127/255)/255);
            if(shot&&speed==0){char path[4096];snprintf(path,sizeof(path),"%s.config-enter.bmp",shot);assert(!capture(r,path));}
        }
        settings_panel_draw_at(&p,b,r,start+steps*15);assert(!p.config_motion);
        config_page(&p,0);settings_panel_draw_at(&p,b,r,start+1000);
        uint8_t *original_page=malloc(480*2560);assert(original_page);memcpy(original_page,p.image.pixels,480*2560);
        config_switch(&p,b,1);start=p.config_clock;
        if(steps){
            assert(p.config_motion==2&&p.setting_page==0);
            settings_panel_draw_at(&p,b,r,start+steps/2*15);assert(p.config_step==steps/2&&p.setting_page==0);
            /* Original preview hole closes while the page moves away. */
            assert(p.image.pixels[190*p.image.stride+360*4]==24);
            settings_panel_draw_at(&p,b,r,start+steps*15);assert(p.config_motion==3&&p.setting_page==1);
            settings_panel_draw_at(&p,b,r,start+steps*15+steps/2*15);
            if(shot&&speed==0){char path[4096];snprintf(path,sizeof(path),"%s.config-page.bmp",shot);assert(!capture(r,path));}
        }
        settings_panel_draw_at(&p,b,r,start+steps*30);assert(!p.config_motion&&p.setting_page==1);
        uint8_t *animated=malloc(480*2560);assert(animated);memcpy(animated,p.image.pixels,480*2560);
        config_page(&p,1);settings_panel_draw_at(&p,b,r,start+2000);assert(!memcmp(animated,p.image.pixels,480*2560));free(animated);
        config_switch(&p,b,0);settings_panel_draw_at(&p,b,r,p.config_clock+2000);
        assert(!memcmp(original_page,p.image.pixels,480*2560));free(original_page);
        config_page(&p,1);config_switch(&p,b,3);start=p.config_clock;
        settings_panel_draw_at(&p,b,r,start+steps/2*15);
        if(shot&&speed==0){char path[4096];snprintf(path,sizeof(path),"%s.config-voice-fade.bmp",shot);assert(!capture(r,path));}
        settings_panel_draw_at(&p,b,r,start+steps*15);assert(!p.config_motion&&p.setting_page==3);
        config_switch(&p,b,1);config_motion_tick(&p,p.config_clock+2000);assert(!p.config_motion&&p.setting_page==1);
        config_close(&p,b,0);if(steps)assert(p.kind==8&&p.config_motion==4);
        config_motion_tick(&p,p.config_clock+2000);assert(!p.kind&&!p.setting_ready);
    }
    /* Flag4000 overrides instant speed and disables keyboard fast finish. */
    b->vm->globals[0][50].number|=0x4000;p.kind=8;settings_panel_init(&p,b);
    assert(p.config_motion==1&&p.config_steps==24);config_motion_skip(&p,b);assert(p.config_motion==1);
    b->vm->globals[0][50].number&=~0x4000;config_motion_skip(&p,b);assert(!p.config_motion);
    assert(bootstrap_message_setting(b,8,0)==2);assert(bootstrap_message_setting(b,8,-2)==0);
    config_page(&p,0);config_switch(&p,b,1);
    /* A finger pressed during an animation cannot activate the new page. */
    MenuTouch touch={0};SaveMenu menu={0};SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.fingerId=8;
    e.tfinger.x=(float)(160+580*1.5)/1280;e.tfinger.y=63.0f/480;
    assert(menu_touch_event(&touch,&menu,&p,b,&e,0));int toggle=p.setting_values[6];
    config_motion_tick(&p,p.config_clock+2000);e.type=SDL_FINGERUP;
    assert(menu_touch_event(&touch,&menu,&p,b,&e,300)&&p.setting_values[6]==toggle);
    assert(bootstrap_message_setting(b,8,(int)original)==(int)original);
    for(unsigned i=0;i<5;i++)rmt_free(&p.config_art[i]);rmt_free(&p.image);SDL_DestroyTexture(p.texture);kconfig_audio_clear(&p.config_audio);bootstrap_destroy(b);
    puts("Kisaku config motion: 12/6/0 and flag4000 24 steps, entry/exit/page/voice fades, exact settled pixels and input isolation: PASS");
}
static void test_config_audio(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    MessagePanel p={.kind=8};settings_panel_init(&p,b);config_page(&p,1);
    const char *names[]={"bgm01.wav","Akikaze.wav","z09588.ogg","Piss.wav"};
    const unsigned volumes[]={3,4,2,KSET_H_VOLUME},toggles[]={6,7,5,19};
    uint8_t out[4096];
    for(unsigned group=0;group<4;group++){
        int stored=bootstrap_message_setting(b,volumes[group],0);
        p.setting_values[toggles[group]]=1;
        config_drag(&p,b,(int)(group+4)*256+2,512);
        assert(p.config_audio.active&&p.config_audio.temporary&&p.config_audio.group==group);
        assert(bootstrap_message_setting(b,volumes[group],0)==stored);
        Ai6Archive *arc=group==0?&b->music:group==2?&b->voice:&b->effects;
        uint8_t *data=NULL,*pcm=NULL;size_t size=0,bytes=0;
        assert(!ai6_read_named(arc,names[group],&data,&size)&&!kaudio_decode(data,size,&pcm,&bytes));free(data);
        assert(bytes==p.config_audio.sample_sizes[group]&&!memcmp(pcm,p.config_audio.samples[group],bytes));
        size_t at=0;while(at+sizeof(out)<=bytes){unsigned audible=0;for(unsigned j=0;j<sizeof(out);j++)audible|=pcm[at+j];if(audible)break;at+=sizeof(out);}assert(at+sizeof(out)<=bytes);
        p.config_audio.sources[0].at=at;
        assert(kconfig_audio_read(&p.config_audio,p.setting_values,out,sizeof(out))==sizeof(out)&&!memcmp(out,pcm+at,sizeof(out)));
        p.setting_values[volumes[group]]=52;p.config_audio.sources[0].at=at;
        assert(kconfig_audio_read(&p.config_audio,p.setting_values,out,sizeof(out))==sizeof(out));
        double gain=pow(10.0,kisaku_sound_volume_db(52,1)/2000.0);
        for(unsigned j=0;j<sizeof(out);j+=2){int16_t src=(int16_t)(pcm[at+j]|(unsigned)pcm[at+j+1]<<8),dst=(int16_t)(out[j]|(unsigned)out[j+1]<<8);assert(dst==(int16_t)(src*gain));}
        p.setting_values[toggles[group]]=0;p.config_audio.sources[0].at=at;
        assert(kconfig_audio_read(&p.config_audio,p.setting_values,out,sizeof(out))==sizeof(out));
        for(unsigned j=0;j<sizeof(out);j++)assert(!out[j]);
        kconfig_audio_release(&p.config_audio);assert(!p.config_audio.active);
        config_drag(&p,b,(int)(group+4)*256+2,512);assert(!p.config_audio.active); /* Disabled slider. */
        p.setting_values[toggles[group]]=1;free(pcm);
    }
    /* An existing sound is auditioned from the paused clock, not replaced
       by a sample, and ordinary effect gain never includes channel 3. */
    uint8_t tone[]={0x10,0x27,0xf0,0xd8,0x20,0x4e,0xe0,0xb1};
    b->effect_tracks[2]=(KEffectTrack){.pcm=tone,.size=sizeof(tone),.loop_end=sizeof(tone)};
    b->effect_tracks[3]=(KEffectTrack){.pcm=tone,.size=sizeof(tone),.loop_end=sizeof(tone)};
    p.setting_values[4]=104;p.setting_values[7]=1;p.setting_values[KSET_H_VOLUME]=0;
    assert(!kconfig_audio_begin(&p.config_audio,b,1)&&!p.config_audio.temporary&&p.config_audio.count==1);
    assert(kconfig_audio_read(&p.config_audio,p.setting_values,out,24)==24);
    for(unsigned j=0;j<24;j++)assert(out[j]==tone[j%sizeof(tone)]);
    assert(!b->effect_tracks[2].read&&!b->effect_tracks[2].clock_position&&!b->effect_tracks[3].read);
    kconfig_audio_release(&p.config_audio);assert(p.config_audio.active); /* Only temporary sounds stop on release. */
    config_page(&p,0);assert(!p.config_audio.active);
    memset(&b->effect_tracks[2],0,2*sizeof(KEffectTrack));
    /* A pending gameplay voice decode must never be stolen for audition. */
    b->voice_loading=1;assert(!kconfig_audio_begin(&p.config_audio,b,2)&&!p.config_audio.active&&b->voice_loading);b->voice_loading=0;
    KConfigAudio missing={0};Ai6Archive effects=b->effects;b->effects=(Ai6Archive){0};
    assert(kconfig_audio_begin(&missing,b,1)<0&&!missing.active&&!b->error[0]);b->effects=effects;kconfig_audio_clear(&missing);
    config_page(&p,1);assert(!config_assets(&p,b));
    unsigned slider=0;while(slider<p.config_area_count[1]&&p.config_areas[1][slider].id!=5*256+2)slider++;
    assert(slider<p.config_area_count[1]);p.selected=slider;p.config_hover=5*256+2;
    settings_panel_action(&p,b,4);assert(p.config_audio.active&&p.config_audio.until>SDL_GetTicks64());
    MenuTouch touch={0};SaveMenu menu={0};SDL_Event event={0};event.type=SDL_FINGERDOWN;event.tfinger.fingerId=11;
    event.tfinger.x=(float)(160+440*1.5)/1280;event.tfinger.y=(float)(p.config_areas[1][slider].y0+1)/480;
    assert(menu_touch_event(&touch,&menu,&p,b,&event,100)&&touch.drag==5*256+3&&p.config_audio.active&&!p.config_audio.until);
    event.type=SDL_FINGERUP;assert(menu_touch_event(&touch,&menu,&p,b,&event,200)&&!p.config_audio.active);
    /* Shared SDL output restores precisely the gameplay queue when closed. */
    assert(!SDL_InitSubSystem(SDL_INIT_AUDIO));
    SDL_AudioSpec spec={0};spec.freq=44100;spec.format=AUDIO_S16LSB;spec.channels=2;spec.samples=512;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);
    KHistoryAudio saved={0};uint8_t queued[1024];memset(queued,0x35,sizeof(queued));
    assert(!khistory_audio_queue(&saved,device,queued,sizeof(queued)));
    p.setting_values[7]=1;config_drag(&p,b,5*256+2,512);
    assert(!settings_panel_audio(&p,&saved,device,44100,2)&&saved.suspended&&p.config_audio.output_owned);
    assert(saved.saved_size==sizeof(queued)&&!memcmp(saved.saved,queued,sizeof(queued)));
    unsigned serial=0;assert(!history_voice_audio(&p,&saved,device,44100,2,&serial)&&saved.suspended);
    config_close(&p,b,0);config_motion_tick(&p,p.config_clock+2000);assert(!p.kind);
    assert(!settings_panel_audio(&p,&saved,device,44100,2)&&!saved.suspended&&!p.config_audio.output_owned);
    assert(SDL_GetQueuedAudioSize(device)==sizeof(queued)&&!memcmp(saved.tail,queued,sizeof(queued)));
    SDL_CloseAudioDevice(device);kconfig_audio_clear(&p.config_audio);
    for(unsigned i=0;i<5;i++)rmt_free(&p.config_art[i]);bootstrap_destroy(b);
    puts("Kisaku config audition: four native samples, live draft gain, channel3 isolation, release/cancel, untouched story cursors and exact SDL queue restore: PASS");
}
static void test_config(MessagePanel *p,KBootstrap *b,SDL_Renderer *r,const char *shot){
    b->reset_pending=0;b->vm->globals[0][61].number=0;
    p->kind=8;p->setting_ready=0;settings_panel_init(p,b);
    for(unsigned i=0;i<KSET_COUNT;i++)p->setting_values[i]=bootstrap_setting_default(i);
    assert(bootstrap_setting_count()==KSET_COUNT&&KSET_COUNT==64&&!config_assets(p,b));
    const unsigned counts[]={22,27,26,68};
    for(unsigned page=0;page<4;page++){
        assert(p->config_area_count[page]==counts[page]);config_page(p,page);
        settings_panel_draw(p,b,r);assert(p->texture&&!p->status[0]);
        if(shot){char path[4096];snprintf(path,sizeof(path),"%s.config%u.bmp",shot,page);assert(!capture(r,path));}
        for(unsigned i=0;i<counts[page];i++){
            ConfigArea a=p->config_areas[page][i];
            settings_panel_pointer(p,b,a.x0,a.y0,0);assert(p->config_hover==a.id);
            settings_panel_pointer(p,b,a.x1-1,a.y1-1,0);assert(p->config_hover==a.id);
        }
    }
    /* Actual art at native positions; preview Alpha differs from CMesWnd. */
    config_page(p,0);memset(b->layers[0].pixels,100,640*480*4);settings_panel_draw(p,b,r);
    assert(p->image.pixels[0]==24);
    int alpha=(int)((224-p->setting_values[9])*(255.0/224.0));
    const uint8_t *preview=p->image.pixels+197*p->image.stride+400*4;
    for(unsigned c=0;c<3;c++)assert(preview[c]==100*(255-alpha)/255);
    int red=bootstrap_message_setting(b,11,0);
    assert(settings_panel_pointer(p,b,616,110,1)==1&&p->setting_values[11]==224);
    assert(bootstrap_message_setting(b,11,0)==red); /* Drag is a draft. */
    config_drag(p,b,0,-999);assert(!p->setting_values[11]);
    settings_panel_pointer(p,b,495,287,0);settings_panel_action(p,b,5);assert(p->setting_values[0]==87);
    p->setting_values[17]=0;config_drag(p,b,770,512);assert(p->setting_values[1]==52);
    settings_panel_pointer(p,b,530,400,1);assert(p->setting_values[17]==1);
    config_drag(p,b,770,999);assert(p->setting_values[1]==104);
    config_page(p,2);int movie=p->setting_values[21];b->vm->bytes[4007]=1;
    settings_panel_pointer(p,b,movie?600:540,117,1);assert(p->setting_values[21]==movie);b->vm->bytes[4007]=0;
    b->vm->globals[0][61].number=1;int difficulty=p->setting_values[KSET_MINIGAME_DIFFICULTY];
    settings_panel_pointer(p,b,600,320,1);assert(p->setting_values[KSET_MINIGAME_DIFFICULTY]==difficulty);b->vm->globals[0][61].number=0;
    config_page(p,1);settings_panel_pointer(p,b,400,400,1);config_motion_tick(p,p->config_clock+2000);assert(p->setting_page==3);
    settings_panel_pointer(p,b,270,50,1);assert(!p->setting_values[KSET_CHARACTER_VOICE]);
    settings_panel_action(p,b,1);config_motion_tick(p,p->config_clock+2000);assert(p->setting_page==1&&p->setting_values[KSET_CHARACTER_VOICE]==1);
    settings_panel_pointer(p,b,400,400,1);config_motion_tick(p,p->config_clock+2000);settings_panel_pointer(p,b,270,50,1);
    settings_panel_pointer(p,b,480,434,1);config_motion_tick(p,p->config_clock+2000);assert(p->setting_page==1&&!p->setting_values[KSET_CHARACTER_VOICE]);
    settings_panel_action(p,b,1);config_motion_tick(p,p->config_clock+2000);assert(!p->kind&&bootstrap_message_setting(b,KSET_CHARACTER_VOICE,0)==1);
    p->kind=8;settings_panel_init(p,b);config_page(p,0);
    /* Touch dragging continues beyond the slider but cannot activate OK. */
    MenuTouch touch={0};SaveMenu menu={0};SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.fingerId=7;
    e.tfinger.x=(float)(160+500*1.5)/1280;e.tfinger.y=110.0f/480;
    assert(menu_touch_event(&touch,&menu,p,b,&e,0)&&touch.drag==1);
    e.type=SDL_FINGERMOTION;e.tfinger.x=0.99f;e.tfinger.y=460.0f/480;
    assert(menu_touch_event(&touch,&menu,p,b,&e,100)&&p->setting_values[11]==224);
    e.type=SDL_FINGERUP;assert(menu_touch_event(&touch,&menu,p,b,&e,200)&&p->kind==8);
    p->setting_values[KSET_CHARACTER_VOICE]=0;p->setting_values[16]=p->setting_values[17]=1;
    uint8_t sentinel=b->vm->bytes[8190];config_activate(p,b,13*256);
    config_motion_tick(p,p->config_clock+2000);
    assert(!p->kind&&bootstrap_message_setting(b,11,0)==224&&bootstrap_message_setting(b,KSET_CHARACTER_VOICE,0)==0&&b->vm->bytes[8190]==sentinel);
    KBootstrap *reload=bootstrap_create_split(b->root,b->save_root);assert(reload&&!reload->error[0]);
    assert(bootstrap_message_setting(reload,11,0)==224&&bootstrap_message_setting(reload,KSET_CHARACTER_VOICE,0)==0);bootstrap_destroy(reload);
    /* Fail to save without discarding the draft or applying live settings. */
    p->kind=8;settings_panel_init(p,b);config_motion_tick(p,p->config_clock+2000);p->setting_values[11]=17;
    char save_root[sizeof(b->save_root)];memcpy(save_root,b->save_root,sizeof(save_root));snprintf(b->save_root,sizeof(b->save_root),"/dev/null/kisaku-config");
    config_activate(p,b,13*256);assert(p->kind==8&&p->status[0]&&p->setting_values[11]==17&&bootstrap_message_setting(b,11,0)==224);
    memcpy(b->save_root,save_root,sizeof(save_root));
    /* Restore defaults so the test save directory does not affect later probes. */
    settings_panel_action(p,b,7);config_activate(p,b,13*256);config_motion_tick(p,p->config_clock+2000);assert(!p->kind);
    puts("Kisaku native settings: 4 pages, actual area edges, sliders, disabled controls, character drafts, persistence and failed-save rollback: PASS");
}
static void letter_exit_dialogue(KBootstrap *b){
    for(unsigned i=0;i<10000;i++){
        int rc=bootstrap_run(b,100000);if(rc<0){fprintf(stderr,"letter exit fixture: %s\n",b->error);abort();}
        if(b->flag_dialog.active)bootstrap_pointer(b,300,350,1);
        if(b->message_active&&!b->message_slide&&!b->message_revealing)return;
        bootstrap_frame(b);
    }
    assert(!"letter fixture timeout");
}
static void letter_exit_open(MessagePanel *p,SaveMenu *menu,KBootstrap *b){
    game_cancel(menu,p,b);assert(b->message_user_hidden&&!b->letter_exit_pending&&!p->kind&&!menu->active);
    game_cancel(menu,p,b);assert(b->letter_exit_pending&&b->message_request==19&&!p->scene_cancel);
    p->kind=b->message_request;b->message_request=0;p->viewing=0;p->status[0]=0;
}
static void test_letter_exit(const char *root,const char *saves,SDL_Renderer *r,const char *shot){
    for(unsigned mode=0;mode<2;mode++){
        char folder[4096];snprintf(folder,sizeof(folder),"%s/letter-exit-%u",saves,mode);assert(!mkdir(folder,0700));
        KBootstrap *b=bootstrap_create_split(root,folder);assert(b&&!b->error[0]);
        for(unsigned i=0;!b->title.active||b->title.age<64;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
        bootstrap_title_move(b,1);bootstrap_confirm(b);letter_exit_dialogue(b);bootstrap_confirm(b);
        uint8_t *data=NULL;size_t size=0;assert(!ai6_read_named(&b->scripts,"memo.mes",&data,&size));
        int id=kvm_add_module(b->vm,"memo.mes",data,size);assert(id>=0);b->module_data[id]=data;
        b->vm->bytes[519]=0;b->vm->bytes[4010]=1;assert(!kvm_start(b->vm,id));letter_exit_dialogue(b);
        assert(b->letter_active&&b->message_read_id==27680);
        assert(bootstrap_message_setting(b,8,2-bootstrap_message_setting(b,8,0))==2);
        MessagePanel p={0};SaveMenu menu={0};MenuTouch touch={0};
        int module=b->vm->module;size_t ip=b->vm->ip;unsigned sp=b->vm->sp;
        letter_exit_open(&p,&menu,b);message_panel_draw(&p,b,r);
        assert(p.viewing&&p.selected==2&&p.texture&&p.dialog_artwork.height>=312);
        /* Confirm the native mode7 prompt is used, at its exact body offset. */
        unsigned opaque=0;
        for(unsigned y=0;y<24;y++)for(unsigned x=0;x<276;x++){
            const uint8_t *s=p.dialog_artwork.pixels+(288+y)*p.dialog_artwork.stride+x*4;
            if(s[3]==255){assert(!memcmp(s,p.dialog_body.pixels+(20+y)*p.dialog_body.stride+(76+x)*4,4));opaque++;}
        }assert(opaque);
        if(shot&&!mode){char path[4096];snprintf(path,sizeof(path),"%s.letter-exit.bmp",shot);assert(!capture(r,path));}
        bootstrap_confirm(b);bootstrap_cancel(b);assert(b->letter_exit_pending&&b->vm->ip==ip&&b->vm->sp==sp);
        message_panel_action(&p,b,0);assert(p.kind==19&&b->letter_exit_pending); /* No default Yes. */
        message_panel_action(&p,b,5);message_panel_action(&p,b,0);
        assert(!p.kind&&!b->letter_exit_pending&&!b->message_user_hidden&&b->letter_active&&b->vm->ip==ip&&b->vm->sp==sp);
        letter_exit_open(&p,&menu,b);message_dialog_pointer(&p,b,0,0,2);
        assert(!p.kind&&!b->message_user_hidden&&b->letter_active); /* Right-click cancels. */
        letter_exit_open(&p,&menu,b);
        SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.fingerId=77;e.tfinger.x=(float)(160+240*1.5)/1280;e.tfinger.y=260.0f/480;
        assert(menu_touch_event(&touch,&menu,&p,b,&e,0));e.type=SDL_FINGERUP;
        assert(menu_touch_event(&touch,&menu,&p,b,&e,700));assert(!p.kind&&!b->message_user_hidden&&b->letter_active); /* Hold cancels. */
        letter_exit_open(&p,&menu,b);b->vm->globals[1][61].number=(int)mode;
        /* Failed resource lookup keeps the modal, original script and stack. */
        Ai6Archive archive=b->scripts;b->scripts=(Ai6Archive){0};message_panel_action(&p,b,4);message_panel_action(&p,b,0);
        assert(p.kind==19&&p.status[0]&&b->letter_exit_pending&&b->vm->module==module&&b->vm->ip==ip&&b->vm->sp==sp&&!b->error[0]);b->scripts=archive;
        p.status[0]=0;
        e.type=SDL_FINGERDOWN;assert(menu_touch_event(&touch,&menu,&p,b,&e,1000));e.type=SDL_FINGERUP;
        assert(menu_touch_event(&touch,&menu,&p,b,&e,1100));
        assert(!p.kind&&!b->letter_active&&!b->message_active&&!b->letter_exit_pending&&!b->quit_requested&&!p.return_title);
        assert(!strcmp(b->vm->modules[b->vm->module].name,mode?"hage_scmode.mes":"scene.mes"));
        assert(!b->vm->ip&&!b->vm->sp&&!b->vm->depth&&!b->vm->script_depth&&b->vm->status==KVM_READY);
        /* 31/320 now owns a real modal selector.  Verify the native AKB layers
           are decoded and that cancelling returns a value to the VM. */
        unsigned selector_seen=0;
        for(unsigned frame=0;frame<300;frame++){
            int state=bootstrap_run(b,100000);
            if(state<0){fprintf(stderr,"Selector follow-up mode=%u: %s; stack=%u\n",mode,b->error,b->vm->sp);break;}
            if(b->scene_panel_request==3){selector_seen=1;break;}
            if(state==0||b->message_active||b->title.active||b->choice_active)break;
            bootstrap_frame(b);
        }
        assert(selector_seen&&b->scene_modal);
        p.kind=20;p.direct_scene=1;p.direct_count=0;p.status[0]=0;message_panel_draw(&p,b,r);
        assert(p.direct_artwork.pixels&&p.direct_artwork.width==496&&p.direct_parts.pixels);
        message_panel_action(&p,b,1);assert(!p.kind&&!b->scene_modal&&b->vm->sp==1&&b->vm->stack[0].number==-1);
        if(!mode){
            assert(!khistory_register(&b->scene_history,b->save_root,1,1,"s01.mes",0));
            assert(!khistory_completion(&b->scene_history,b->save_root,1,1,1,NULL));
            b->vm->sp=0;p.kind=20;p.direct_scene=1;p.direct_count=0;p.direct_selected=0;p.status[0]=0;
            message_panel_action(&p,b,0);
            assert(p.kind==16&&p.scene_request==1&&!b->scene_modal&&b->vm->sp==1&&b->vm->stack[0].number==1);
            p.kind=0;p.scene_request=0;b->vm->sp=0;
        }
        SDL_DestroyTexture(p.texture);rmt_free(&p.image);rmt_free(&p.dialog_artwork);rmt_free(&p.dialog_body);rmt_free(&p.direct_artwork);rmt_free(&p.direct_parts);bootstrap_destroy(b);
    }
    puts("Kisaku letter replay exit and 31/320 selector: native mode7 atlas, scene catalog lock state, AKB selector layers, cancel/result and both selector script targets: PASS");
}

int main(int argc,char **argv){
    assert(argc==3||argc==4);
    assert(!SDL_Init(SDL_INIT_VIDEO));
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_BGRA32);assert(surface);
    SDL_Renderer *renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    KBootstrap *b=bootstrap_create_split(argv[1],argv[2]);assert(b&&!b->error[0]);
    int rc=bootstrap_run(b,100000);
    for(unsigned frame=0;rc==1&&frame<1000&&!(b->title.active&&b->title.age>=64);frame++){
        bootstrap_frame(b);rc=bootstrap_run(b,100000);
    }
    assert(rc==1&&b->title.active&&b->title.age>=64);
    MessagePanel p={.kind=6};SaveMenu menu={0};MenuTouch touch={0};
    message_panel_draw(&p,b,renderer);
    assert(p.viewing==1&&p.selected==2&&p.texture&&p.dialog_artwork.pixels&&p.dialog_body.pixels);
    if(argc==4)assert(!capture(renderer,argv[3]));
    message_panel_action(&p,b,0);assert(p.kind==6&&!b->quit_requested); /* No default Yes. */
    message_panel_action(&p,b,5);assert(p.selected==0);
    message_panel_action(&p,b,0);assert(!p.kind&&!b->quit_requested&&b->message_hover==-1);
    p.kind=6;p.viewing=0;message_dialog_pointer(&p,b,231,260,1);assert(p.kind==6&&p.selected==2);
    message_dialog_pointer(&p,b,240,260,0);assert(p.selected==1&&!b->quit_requested);
    message_dialog_pointer(&p,b,0,0,2);assert(!p.kind&&!b->quit_requested);
    p.kind=18;p.viewing=0;message_panel_action(&p,b,4);assert(p.selected==1);
    message_panel_action(&p,b,0);assert(!p.kind&&p.return_title&&!b->quit_requested);p.return_title=0;
    p.kind=6;p.viewing=1;p.selected=2;
    SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.fingerId=1;
    e.tfinger.x=(float)(160+240*1.5)/1280;e.tfinger.y=260.0f/480;
    assert(menu_touch_event(&touch,&menu,&p,b,&e,0)&&!b->quit_requested);
    e.type=SDL_FINGERUP;assert(menu_touch_event(&touch,&menu,&p,b,&e,700));
    assert(!p.kind&&!b->quit_requested); /* Long press cancels. */
    p.kind=6;p.viewing=1;e.type=SDL_FINGERDOWN;
    assert(menu_touch_event(&touch,&menu,&p,b,&e,1000));
    p.kind=18;e.type=SDL_FINGERUP;
    assert(menu_touch_event(&touch,&menu,&p,b,&e,1100));
    assert(p.kind==18&&!p.return_title&&!b->quit_requested); /* A contact cannot cross modal owners. */
    p.kind=6;p.viewing=1;e.type=SDL_FINGERDOWN;
    assert(menu_touch_event(&touch,&menu,&p,b,&e,1200));e.type=SDL_FINGERUP;
    assert(menu_touch_event(&touch,&menu,&p,b,&e,1300));assert(!p.kind&&b->quit_requested);
    b->quit_requested=0;b->reset_pending=1;p.kind=6;p.viewing=0;
    message_dialog_pointer(&p,b,240,260,1);
    assert(p.kind==6&&p.status[0]&&!b->quit_requested); /* Failed progress flush keeps the dialog open. */
    test_native_quit(argv[1],argv[2]);
    test_config(&p,b,renderer,argc==4?argv[3]:NULL);
    test_config_audio(argv[1],argv[2]);
    test_config_motion(argv[1],argv[2],renderer,argc==4?argv[3]:NULL);
    test_backlog(&p,b,renderer,argc==4?argv[3]:NULL);
    test_native_backlog(argv[1],argv[2],renderer);
    test_save_menu(argv[1],argv[2],renderer,argc==4?argv[3]:NULL);
    test_letter_exit(argv[1],argv[2],renderer,argc==4?argv[3]:NULL);
    test_name_editor(argv[1],argv[2],renderer);
    test_appendix_media(argv[1],argv[2],renderer);
    for(unsigned i=0;i<5;i++)rmt_free(&p.config_art[i]);
    kconfig_audio_clear(&p.config_audio);
    SDL_DestroyTexture(p.texture);rmt_free(&p.image);rmt_free(&p.dialog_artwork);rmt_free(&p.dialog_body);
    rmt_free(&p.appendix_artwork);rmt_free(&p.appendix_parts);
    bootstrap_destroy(b);SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);SDL_Quit();
    puts("Kisaku frontend confirmation: native render, keyboard/mouse/touch, modal ownership and failed-flush handling: PASS");
    return 0;
}

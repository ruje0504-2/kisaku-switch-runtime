/* Included after the dispatcher; loading is applied only to a fresh initialized
   runtime so stale image jobs, animations, audio and dialogue cannot survive. */
int bootstrap_can_save(const KBootstrap *b){
    if(!b||b->native_wait_clock||bootstrap_bowling_active(b)||b->exec523_active||b->exec522_motion||b->param_animation_active||b->vm->byte_count!=9192||b->vm->word_count!=600||b->raw_size!=15000||b->vm->global_count[1]<=61||b->vm->globals[1][61].string||(unsigned)b->vm->globals[1][61].number>1||b->quit_modal||b->quit_requested||b->load_modal||b->scene_modal||b->scene_replay||b->error[0]||b->restore_pending||b->history_restore||b->ax_modal||b->ax_extra_modal||!b->message_active||b->message_timed||b->message_slide||b->message_buttons_motion||b->novel_transition||b->letter_transition||b->image_loading||b->choice_active)return 0;
    /* Letter checkpoints reconstruct a page from its first paragraph. Only
       publish a visible, settled prompt with a stable native read ID. */
    if(b->letter_mode&&(!b->letter_active||b->message_revealing||b->message_user_hidden||b->letter_backlog||b->message_request||b->message_read_id<0))return 0;
    unsigned base;if(saved_frame_depth(b->vm,&base))return 0;
    int id=b->vm->depth>base?b->vm->frames[base].module:b->vm->module;
    size_t ip;return b->vm->globals[0][48].number>=0&&!kvm_checkpoint_offset(b->vm,id,(unsigned)b->vm->globals[0][48].number,&ip);
}
static int save_runtime_to(KBootstrap *b,unsigned slot,const char *utf8,const char *destination){
    if(!bootstrap_can_save(b)||slot<1||slot>100)return -1;
    uint8_t note[33]={0};size_t note_size=0;
    if(utf8&&utf8[0]&&(ktext_name_encode(utf8,note,&note_size)||note_size>27))return -1;
    if(store_native_progress(b))return -1;
    KVM *v=b->vm;unsigned selector=(unsigned)v->globals[1][61].number;
    KControlRecord records[29];KValue values[29][3];unsigned count=0;
    for(unsigned i=0;i<b->saved_control_count;i++)if(b->saved_controls[i].type<0xfffb)records[count++]=b->saved_controls[i];
    /* A fresh runtime has the title AX atlas. Native load keeps resident
       resources, and some scripts restore tracks without recording the
       previous AX load (e.g. LEV00_1 / EV04A). Preserve that resource
       context as a port extension, never as a guessed native control. */
    KValue context[3]={{1,NULL},{0,b->ax.name},{0,b->normal_atlas_name}};
    if(b->ax.size&&b->ax.name[0]&&b->normal_atlas_name[0]){
        if(count>=29)return -1;
        records[count++]=(KControlRecord){0,0xfffb,context,3};
    }
    unsigned base;if(saved_frame_depth(v,&base))return -1;
    unsigned outer_record=count;
    if(base){if(count>=29)return -1;records[count++]=(KControlRecord){0,0xfffc,NULL,0};}
    if(count>=29||b->history_count>64)return -1;
    unsigned history_at=count++;
    records[history_at]=(KControlRecord){0,0xfffd,NULL,0};
    KValue resume[3]={{1,NULL},{b->message_read_id,NULL},{b->letter_mode?2:1,NULL}};
    if(b->novel_mode||b->letter_mode){
        if(b->message_read_id<0||count>=28)return -1;
        records[count++]=(KControlRecord){0,0xfffe,resume,3};
    }
    if(count+v->script_depth+1>29)return -1;
    int active=v->depth>base?v->frames[base].module:v->module;
    for(unsigned i=0;i<=v->script_depth;i++){
        int id=i<v->script_depth?v->scripts[i].module:active;
        values[i][0]=(KValue){0,v->modules[id].name};
        values[i][1]=(KValue){i<v->script_depth?(int32_t)v->scripts[i].ip:v->globals[0][48].number,NULL};
        values[i][2]=(KValue){i==v->script_depth,NULL};
        records[count++]=(KControlRecord){(uint16_t)i,0xffff,values[i],3};
    }
    KFlags flags={0};snprintf((char *)flags.module,sizeof(flags.module),"%.259s",v->modules[active].name);
    for(unsigned i=0;i<2;i++){flags.globals[i]=v->globals[i];flags.counts[i]=v->global_count[i];}
    flags.bytes=v->bytes;flags.byte_count=v->byte_count;flags.words=v->words;flags.word_count=v->word_count;flags.raw=b->raw_variables;flags.raw_count=(unsigned)b->raw_size;
    KValue *outer=NULL;
    if(base){outer=saved_frame_values(v,&records[outer_record].count);if(!outer)return -1;records[outer_record].values=outer;}
    /* Publish metadata with the paired save; a failed write must leave both
       the live catalog and the previous committed slot unchanged. */
    size_t raw_size=b->raw_size;
    uint8_t *raw=calloc(1,raw_size);if(!raw){free(outer);return -1;}
    memcpy(raw,b->raw_variables,b->raw_size);
    /* 478240: raw+1000 is the selected zero-based slot, followed by 45-byte records. */
    raw[1000]=(uint8_t)(slot-1);raw[1001]=0;
    uint8_t *record=raw+1002+(slot-1)*45;record[0]=1;
    unsigned stage=v->bytes[617]==1?3:v->bytes[447]==1?2:v->bytes[103]==1?1:0;
    unsigned day=(uint16_t)(v->globals[0][7].number/100);
    record[1]=(uint8_t)stage;record[2]=0;record[3]=(uint8_t)day;record[4]=(uint8_t)(day>>8);
    time_t stamp=time(NULL);struct tm *tm=localtime(&stamp);
    if(tm){unsigned fields[6]={(unsigned)tm->tm_year+1900,(unsigned)tm->tm_mon+1,(unsigned)tm->tm_mday,(unsigned)tm->tm_hour,(unsigned)tm->tm_min,(unsigned)tm->tm_sec};for(unsigned i=0;i<6;i++){record[5+i*2]=(uint8_t)fields[i];record[6+i*2]=(uint8_t)(fields[i]>>8);}}
    if(utf8){memset(record+17,0,28);memcpy(record+17,note,note_size);}
    flags.raw=raw;flags.raw_count=(unsigned)raw_size;
    KValue bank1[8192];memcpy(bank1,v->globals[1],v->global_count[1]*sizeof(KValue));bank1[60]=(KValue){1,NULL};flags.globals[1]=bank1;
    if(kbacklog_store_pack(b,&records[history_at])){free(raw);free(outer);return -1;}
    int result=kslot_write_state(destination,selector,slot,&flags,records,count,&b->layers[0],&b->layers[4]);free(outer);kbacklog_store_free(&records[history_at]);
    if(!result){free(b->raw_variables);b->raw_variables=raw;b->raw_size=raw_size;v->raw=raw;v->raw_size=raw_size;v->globals[1][60]=(KValue){1,NULL};}else free(raw);
    return result;
}
int bootstrap_save_slot(KBootstrap *b,unsigned slot){return bootstrap_save_slot_comment(b,slot,NULL);}
int bootstrap_save_slot_comment(KBootstrap *b,unsigned slot,const char *utf8){return b?save_runtime_to(b,slot,utf8,bootstrap_save_dir(b)):-1;}
int bootstrap_prepare_animation_switch(KBootstrap *b,unsigned mode){
    /* The reference game's paired _m scripts do not exist in Kisaku. */
    (void)b;(void)mode;return -1;
}
static void restored_windows_free(KBootstrap *state){
    if(!state)return;
    rmt_free(&state->param_surface);rmt_free(&state->param_atlas);rmt_free(&state->diary_surface);free(state);
}
/* 507b69..507d3f rebuilds private auxiliary windows from slot words.
   Stage all allocations before replacing any live VM or window state. */
static KBootstrap *restored_windows_build(KBootstrap *b,const KFlags *f){
    KBootstrap *state=calloc(1,sizeof(*state));if(!state)return NULL;
    state->vm=b->vm;state->images=b->images;
    state->param_total=b->param_total;state->param_remaining=b->param_remaining;
    for(unsigned i=0;i<4;i++){
        int value=(int16_t)f->words[500+i],limit=i==3?80:999;
        if(value<0){restored_windows_free(state);return NULL;}
        state->param_values[i]=(int16_t)(value>limit?limit:value);
    }
    state->param_markers[0]=(int16_t)f->words[552];state->param_markers[1]=(int16_t)f->words[553];
    state->diary_viewport_height=b->diary_viewport_height;state->diary_page_height=b->diary_page_height;state->diary_scroll=b->diary_scroll;
    if(param_window_rows(state,(int16_t)f->words[541],1)||diary_restore(state,f->words)){
        restored_windows_free(state);return NULL;
    }
    return state;
}
static int load_runtime_from(KBootstrap *b,const char *root,unsigned selector,unsigned slot){
    if(!b||!b->title.active||b->image_loading||b->error[0])return -1;
    if(selector>1)return -1;
    KFlags *f=NULL,*catalog=NULL;KControlStore *s=NULL;KBootstrap *restored=NULL;
    uint8_t *context_ax=NULL;size_t context_ax_size=0;KImage context_atlas={0},resident_scene={0};
    const KControlRecord *context=NULL;KBacklogSnapshot *backlog=NULL;
    if(kslot_read(root,selector,slot,&f,&s))return -1;
    if(kslot_scene(root,selector,slot,&resident_scene)<0)goto bad;
    /* Reject empty native slots and inconsistent module/checkpoint metadata. */
    if(!memchr(f->module,0,260)||!f->module[0]||f->counts[0]<51||f->counts[1]!=100||f->byte_count!=9192||f->word_count!=600||f->raw_count!=15000||!s->count||f->globals[1][61].string||f->globals[1][61].number!=(int)selector)goto bad;
    catalog=kflags_read_slot(root,0,selector?201:100);
    if(!catalog||catalog->word_count!=600||catalog->raw_count!=15000||catalog->counts[1]!=100||
       catalog->globals[1][61].string||catalog->globals[1][61].number!=(int)selector||kflags_restore_progress(f,catalog,selector))goto bad;
    kflags_free(catalog);catalog=NULL;
    if(!selector){f->words[99]=0;f->bytes[4090]=(uint8_t)option(b,"CONFIG","Movie",1);}
    for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xfffb){
        const KControlRecord *r=&s->records[i];
        if(context||r->id||r->count!=3||r->values[0].string||r->values[0].number!=1||
           !r->values[1].string||!r->values[1].string[0]||strlen(r->values[1].string)>=64||
           !r->values[2].string||!r->values[2].string[0]||strlen(r->values[2].string)>=261)goto bad;
        context=r;uint8_t *data=NULL;size_t size=0;
        if(read_named(&b->data,r->values[1].string,&context_ax,&context_ax_size)||
           context_ax_size<0x500||context_ax_size>AX_CAPACITY)goto bad;
        int failed=read_named(&b->images,r->values[2].string,&data,&size)||rmt_decode(data,size,&context_atlas);
        free(data);
        if(failed||context_atlas.x<0||context_atlas.y<0||
           (uint64_t)context_atlas.x+context_atlas.width>b->layers[8].width||
           (uint64_t)context_atlas.y+context_atlas.height>b->layers[8].height)goto bad;
    }
    const KControlRecord *history=NULL;
    for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xfffd){
        const KControlRecord *r=&s->records[i];
        if(history||r->id||!r->count||r->values[0].string)goto bad;
        unsigned first=1,flat=0;
        if(r->values[0].number==1){
            if(r->count>129||!(r->count&1))goto bad;
            flat=(r->count-1)/2;
        }else{
            if(kbacklog_store_unpack(r,&backlog))goto bad;
            first=4;flat=(unsigned)r->values[1].number;
        }
        for(unsigned j=0;j<flat*2;j++){
            const KValue *value=&r->values[first+j];const char *text=value->string;
            if(!text||value->number||strlen(text)>(j&1?260u:4096u)||(!(j&1)&&!text[0]))goto bad;
            if((j&1)&&text[0]){unsigned k=0;for(;k<b->voice.count;k++)if(equal(text,b->voice.entries[k].name))break;if(k==b->voice.count)goto bad;}
        }
        history=r;
    }
    int resume_read=-1;unsigned resume_mode=0;
    for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xfffe){
        KControlRecord *r=&s->records[i];
        if(resume_read!=-1||r->id||r->count!=3||r->values[0].string||r->values[1].string||r->values[2].string||r->values[0].number!=1||r->values[1].number<0||(r->values[2].number!=1&&r->values[2].number!=2))goto bad;
        resume_mode=(unsigned)r->values[2].number;resume_read=r->values[1].number;
        if((size_t)resume_read>=b->read_size*8)goto bad;
    }
    KControlRecord *last=&s->records[s->count-1];
    if(last->type!=0xffff||last->count!=3||!last->values[0].string||!equal(last->values[0].string,(char *)f->module)||last->values[2].number!=1)goto bad;
    unsigned chains=0;for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xffff){
        KControlRecord *r=&s->records[i];
        if(r->id!=chains++||r->count!=3||!r->values[0].string||r->values[1].string||r->values[2].string)goto bad;
        char target[261];
        if(krestore_name(target,r->values[0].string))goto bad;
        int id=module(b,target);if(id<0)goto bad;
        size_t ip=(uint32_t)r->values[1].number;
        if(r==last){if(r->values[1].number<0||kvm_checkpoint_offset(b->vm,id,(unsigned)r->values[1].number,&ip))goto bad;}
        else if(r->values[2].number||ip>b->vm->modules[id].size||!b->vm->modules[id].boundaries[ip])goto bad;
    }
    KSavedFrames *outer=NULL;if(saved_frame_load(b,s->records,s->count,chains-1,&outer))goto bad;free(outer);
    if(!selector&&!(restored=restored_windows_build(b,f)))goto bad;
    uint8_t *raw=malloc(f->raw_count?f->raw_count:1);if(!raw)goto bad;
    KControlRecord records[29]={0};
    for(unsigned i=0;i<s->count;i++){
        records[i]=s->records[i];size_t bytes=records[i].count*sizeof(KValue);records[i].values=malloc(bytes?bytes:1);
        if(!records[i].values){for(unsigned j=0;j<i;j++)free(records[j].values);free(raw);goto bad;}
        if(bytes)memcpy(records[i].values,s->records[i].values,bytes);
    }
    if(f->raw_count)memcpy(raw,f->raw,f->raw_count);
    if(resident_scene.pixels){
        for(unsigned y=0;y<480;y++)memcpy(b->layers[4].pixels+y*b->layers[4].stride,resident_scene.pixels+y*2560,2560);
        rmt_free(&resident_scene);
    }
    if(context){
        /* All validation/allocation has finished; both installs are infallible. */
        ax_load(&b->ax,context->values[1].string,context_ax,context_ax_size);
        free(b->animation_data);b->animation_data=context_ax;context_ax=NULL;b->animation_size=context_ax_size;
        snprintf(b->loaded_animation,sizeof(b->loaded_animation),"%s",context->values[1].string);
        install_image(b,8,&context_atlas,context->values[2].string);rmt_free(&context_atlas);
    }
    if(restored){
        rmt_free(&b->param_surface);rmt_free(&b->param_atlas);rmt_free(&b->diary_surface);
        b->param_surface=restored->param_surface;b->param_atlas=restored->param_atlas;b->diary_surface=restored->diary_surface;
        b->param_rows=restored->param_rows;memcpy(b->param_values,restored->param_values,sizeof(b->param_values));
        memcpy(b->param_markers,restored->param_markers,sizeof(b->param_markers));
        memcpy(b->diary_people,restored->diary_people,sizeof(b->diary_people));memcpy(b->diary_events,restored->diary_events,sizeof(b->diary_events));
        b->diary_days=restored->diary_days;b->diary_content_height=restored->diary_content_height;
        b->diary_viewport_height=restored->diary_viewport_height;b->diary_scroll=restored->diary_scroll;
        b->auxiliary_windows_enabled=1;free(restored);restored=NULL;
    }
    KVM *v=b->vm;for(unsigned i=0;i<2;i++){memset(v->globals[i],0,sizeof(v->globals[i]));memcpy(v->globals[i],f->globals[i],f->counts[i]*sizeof(KValue));v->global_count[i]=f->counts[i];}
    memcpy(v->bytes,f->bytes,f->byte_count);v->byte_count=f->byte_count;
    memset(v->words,0,sizeof(v->words));memcpy(v->words,f->words,f->word_count*sizeof(uint16_t));v->word_count=f->word_count;
    /* Backlog buffers are private runtime state, not part of native FLAG.
       Do not finalize the previous runtime's in-progress recording. */
    v->globals[0][50].number=(v->globals[0][50].number&~0x100)|0x1000;
    b->message_index=-1;
    free(b->raw_variables);b->raw_variables=raw;b->raw_size=f->raw_count;v->raw=raw;v->raw_size=f->raw_count;
    for(unsigned i=0;i<b->control_count;i++)free(b->controls[i].values);
    memcpy(b->controls,records,sizeof(records));b->control_count=s->count;
    f->next=b->flag_files;b->flag_files=f;b->flag_file_count++;
    s->next=b->control_files;b->control_files=s;b->control_file_count++;
    b->title.active=0;b->title_load_requested=0;
    b->history_restore=history;kbacklog_snapshot_free(b->backlog_restore);b->backlog_restore=backlog;backlog=NULL;
    b->restore_mode=resume_mode;b->restore_read_id=resume_read;b->restore_pending=resume_read>=0;b->restore_remaining=4096;
    if(b->read_flags&&kread_flags_load(bootstrap_save_dir(b),selector,b->read_flags,b->read_size)<0)return -1;
    b->read_selector=selector;b->read_loaded=1;b->read_dirty=0;
    /* Enter through native restore preamble; it rebuilds visuals before sub10. */
    v->sp=0;v->syscall=28;v->status=KVM_SYSCALL;
    if(kvm_push(v,(KValue){9,NULL})||bootstrap_dispatch(b))return -1;
    return 0;
bad:kbacklog_snapshot_free(backlog);rmt_free(&resident_scene);free(context_ax);rmt_free(&context_atlas);restored_windows_free(restored);kflags_free(catalog);kflags_free(f);kcontrol_free(s);return -1;
}

int bootstrap_load_slot(KBootstrap *b,unsigned selector,unsigned slot){return b?load_runtime_from(b,bootstrap_save_dir(b),selector,slot):-1;}
int bootstrap_load_animation_switch(KBootstrap *b,unsigned selector){
    (void)b;(void)selector;return -1;
}

/* Included after the dispatcher; loading is applied only to a fresh initialized
   runtime so stale image jobs, animations, audio and dialogue cannot survive. */
int bootstrap_can_save(const KBootstrap *b){
    if(!b||b->vm->byte_count!=8192||b->load_modal||b->scene_modal||b->scene_replay||b->error[0]||b->restore_pending||b->history_restore||b->ax_modal||!b->message_active||b->message_timed||b->message_slide||b->novel_transition||b->letter_transition||b->image_loading||b->choice_active)return 0;
    unsigned base;if(saved_frame_depth(b->vm,&base))return 0;
    int id=b->vm->depth>base?b->vm->frames[base].module:b->vm->module;
    size_t ip;return b->vm->globals[0][48].number>=0&&!kvm_checkpoint_offset(b->vm,id,(unsigned)b->vm->globals[0][48].number,&ip);
}
static int save_runtime_to(KBootstrap *b,unsigned slot,const char *utf8,const char *destination,int animation){
    if(!bootstrap_can_save(b)||slot<1||slot>100)return -1;
    uint8_t note[33]={0};size_t note_size=0;
    if(utf8&&utf8[0]&&ktext_name_encode(utf8,note,&note_size))return -1;
    KVM *v=b->vm;unsigned selector=v->bytes[8100];if(selector>3)selector=3;
    if(b->scene){
        if(v->byte_count<5370||v->word_count<381)return -1;
        memcpy(v->bytes+3000,b->scene->visited,1000);memcpy(v->bytes+4500,b->scene->status,370);memcpy(v->bytes+5000,b->scene->flags,370);memcpy(v->words+11,b->scene->counters,sizeof(b->scene->counters));
    }
    KControlRecord records[29];KValue values[29][3];unsigned count=0;
    for(unsigned i=0;i<b->saved_control_count;i++)if(b->saved_controls[i].type<0xfffc)records[count++]=b->saved_controls[i];
    unsigned base;if(saved_frame_depth(v,&base))return -1;
    unsigned outer_record=count;
    if(base){if(count>=29)return -1;records[count++]=(KControlRecord){0,0xfffc,NULL,0};}
    if(count>=29||b->history_count>64)return -1;
    KValue history[129]={{1,NULL}};
    for(unsigned i=0;i<b->history_count;i++){
        unsigned index=(b->history_next+64-b->history_count+i)%64;
        history[1+i*2]=(KValue){0,b->history[index]};history[2+i*2]=(KValue){0,b->history_voice[index]};
    }
    records[count++]=(KControlRecord){0,0xfffd,history,1+b->history_count*2};
    KValue resume[3]={{1,NULL},{b->message_read_id,NULL},{1,NULL}};
    if(b->novel_mode){
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
    size_t raw_size=b->raw_size<5402?5402:b->raw_size;
    uint8_t *raw=calloc(1,raw_size);if(!raw){free(outer);return -1;}
    memcpy(raw,b->raw_variables,b->raw_size);
    uint8_t *record=raw+(slot-1)*54;record[0]=1;
    unsigned scene=kscene_module(v->modules[active].name);record[1]=(uint8_t)scene;record[2]=(uint8_t)(scene>>8);
    time_t stamp=time(NULL);struct tm *tm=localtime(&stamp);
    if(tm){unsigned year=(unsigned)(tm->tm_year+1900);record[3]=(uint8_t)year;record[4]=(uint8_t)(year>>8);record[5]=(uint8_t)(tm->tm_mon+1);record[6]=(uint8_t)tm->tm_mday;record[7]=0;record[8]=(uint8_t)tm->tm_hour;record[9]=0;record[10]=(uint8_t)tm->tm_min;record[11]=0;}
    if(utf8){memset(record+12,0,42);memcpy(record+12,note,note_size);}
    flags.raw=raw;flags.raw_count=(unsigned)raw_size;
    uint8_t alternate_bytes[8192];
    if(animation>=0){
        memcpy(raw,b->raw_variables,b->raw_size); /* Internal snapshot is not a user slot. */
        memcpy(alternate_bytes,v->bytes,8192);alternate_bytes[8190]=(uint8_t)animation;alternate_bytes[8191]=1;flags.bytes=alternate_bytes;
    }
    int result=kslot_write_image(destination,selector,slot,&flags,records,count,animation<0?&b->layers[0]:NULL);free(outer);
    if(!result&&animation<0){free(b->raw_variables);b->raw_variables=raw;b->raw_size=raw_size;if(b->scene&&scene&&scene<=KSCENE_COUNT)b->scene->route_group[scene-1]=(slot-1)/5+1;}else free(raw);
    return result;
}
int bootstrap_save_slot(KBootstrap *b,unsigned slot){return bootstrap_save_slot_comment(b,slot,NULL);}
int bootstrap_save_slot_comment(KBootstrap *b,unsigned slot,const char *utf8){return b?save_runtime_to(b,slot,utf8,bootstrap_save_dir(b),-1):-1;}
int bootstrap_prepare_animation_switch(KBootstrap *b,unsigned mode){
    if(!b||mode>1)return -1;
    if(!bootstrap_can_save(b)||b->vm->bytes[8190]==mode)return 0;
    unsigned base;if(saved_frame_depth(b->vm,&base))return -1;
    int id=b->vm->depth>base?b->vm->frames[base].module:b->vm->module;char target[261];
    if(krestore_name(target,b->vm->modules[id].name,1,1,mode))return -1;
    unsigned entry=0;while(entry<b->scripts.count&&!equal(b->scripts.entries[entry].name,target))entry++;
    /* Ordinary scenes have no paired animation module. Their next script
       consults byte8190; no checkpoint reconstruction is necessary. */
    if(entry==b->scripts.count||equal(target,b->vm->modules[id].name))return 0;
    char root[4096];if(snprintf(root,sizeof(root),"%s/kisaku-menu-resume",bootstrap_save_dir(b))>=(int)sizeof(root))return -1;
    if(mkdir(root,0700)&&errno!=EEXIST)return -1;
    return save_runtime_to(b,1,NULL,root,(int)mode)?-1:1;
}
static int load_runtime_from(KBootstrap *b,const char *root,unsigned selector,unsigned slot){
    if(!b||!b->title.active||b->image_loading||b->error[0])return -1;
    KFlags *f=NULL;KControlStore *s=NULL;KScene *scene=NULL;
    if(kslot_read(root,selector,slot,&f,&s))return -1;
    /* Reject empty native slots and inconsistent module/checkpoint metadata. */
    if(!memchr(f->module,0,260)||!f->module[0]||f->counts[0]<51||f->counts[1]<100||f->byte_count!=8192||f->word_count>8192||f->raw_count>16777216||!s->count)goto bad;
    const KControlRecord *history=NULL;
    for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xfffd){
        const KControlRecord *r=&s->records[i];
        if(history||r->id||!r->count||r->count>129||!(r->count&1)||r->values[0].string||r->values[0].number!=1)goto bad;
        for(unsigned j=1;j<r->count;j++){
            const char *text=r->values[j].string;
            if(!text||r->values[j].number||strlen(text)>(j&1?4096u:260u)||(j&1&&!text[0]))goto bad;
            if(!(j&1)&&text[0]){unsigned k=0;for(;k<b->voice.count;k++)if(equal(text,b->voice.entries[k].name))break;if(k==b->voice.count)goto bad;}
        }
        history=r;
    }
    int resume_read=-1;
    for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xfffe){
        KControlRecord *r=&s->records[i];
        if(resume_read!=-1||r->id||r->count!=3||r->values[0].string||r->values[1].string||r->values[2].string||r->values[0].number!=1||r->values[1].number<0||r->values[2].number!=1)goto bad;
        resume_read=r->values[1].number;
        if((size_t)resume_read>=b->read_size*8)goto bad;
    }
    KControlRecord *last=&s->records[s->count-1];
    if(last->type!=0xffff||last->count!=3||!last->values[0].string||!equal(last->values[0].string,(char *)f->module)||last->values[2].number!=1)goto bad;
    unsigned chains=0;for(unsigned i=0;i<s->count;i++)if(s->records[i].type==0xffff){
        KControlRecord *r=&s->records[i];
        if(r->id!=chains++||r->count!=3||!r->values[0].string||r->values[1].string||r->values[2].string)goto bad;
        char target[261];
        if(krestore_name(target,r->values[0].string,r->values[2].number,f->bytes[8191],f->bytes[8190]))goto bad;
        int id=module(b,target);if(id<0)goto bad;
        size_t ip=(uint32_t)r->values[1].number;
        if(r==last){if(r->values[1].number<0||kvm_checkpoint_offset(b->vm,id,(unsigned)r->values[1].number,&ip))goto bad;}
        else if(r->values[2].number||ip>b->vm->modules[id].size||!b->vm->modules[id].boundaries[ip])goto bad;
    }
    KSavedFrames *outer=NULL;if(saved_frame_load(b,s->records,s->count,chains-1,&outer))goto bad;free(outer);
    uint8_t *map_data=NULL;size_t map_size=0;scene=calloc(1,sizeof(*scene));
    if(!scene||read_named(&b->data,"map.map",&map_data,&map_size)){free(map_data);goto bad;}
    int map_bad=kscene_map(scene,map_data,map_size)||kscene_restore(scene,f->bytes,f->byte_count,f->raw,f->raw_count);free(map_data);
    if(map_bad||f->word_count<381)goto bad;
    if(kscene_path_restore(scene,f->words,f->word_count,(const char *)f->module))goto bad;
    uint8_t *raw=malloc(f->raw_count?f->raw_count:1);if(!raw)goto bad;
    KControlRecord records[29]={0};
    for(unsigned i=0;i<s->count;i++){
        records[i]=s->records[i];size_t bytes=records[i].count*sizeof(KValue);records[i].values=malloc(bytes?bytes:1);
        if(!records[i].values){for(unsigned j=0;j<i;j++)free(records[j].values);free(raw);goto bad;}
        if(bytes)memcpy(records[i].values,s->records[i].values,bytes);
    }
    if(f->raw_count)memcpy(raw,f->raw,f->raw_count);
    KVM *v=b->vm;for(unsigned i=0;i<2;i++){memset(v->globals[i],0,sizeof(v->globals[i]));memcpy(v->globals[i],f->globals[i],f->counts[i]*sizeof(KValue));v->global_count[i]=f->counts[i];}
    memcpy(v->bytes,f->bytes,f->byte_count);v->byte_count=f->byte_count;
    memset(v->words,0,sizeof(v->words));memcpy(v->words,f->words,f->word_count*sizeof(uint16_t));v->word_count=f->word_count;
    v->bytes[8100]=(uint8_t)selector;
    /* Backlog buffers are private runtime state, not part of native FLAG.
       Do not finalize the previous runtime's in-progress recording. */
    v->globals[0][50].number=(v->globals[0][50].number&~0x100)|0x1000;
    b->message_index=-1;
    free(b->raw_variables);b->raw_variables=raw;b->raw_size=f->raw_count;
    free(b->scene);b->scene=scene;scene=NULL;b->scene_ui_pending=1;
    for(unsigned i=0;i<b->control_count;i++)free(b->controls[i].values);
    memcpy(b->controls,records,sizeof(records));b->control_count=s->count;
    f->next=b->flag_files;b->flag_files=f;b->flag_file_count++;
    s->next=b->control_files;b->control_files=s;b->control_file_count++;
    b->title.active=0;b->title_load_requested=0;
    b->history_restore=history;
    b->restore_read_id=resume_read;b->restore_pending=resume_read>=0;b->restore_remaining=4096;
    if(b->read_flags&&kread_flags_load(bootstrap_save_dir(b),selector,b->read_flags,b->read_size)<0)return -1;
    b->read_selector=selector;b->read_loaded=1;b->read_dirty=0;
    /* Enter through native restore preamble; it rebuilds visuals before sub10. */
    v->sp=0;v->syscall=28;v->status=KVM_SYSCALL;
    if(kvm_push(v,(KValue){9,NULL})||bootstrap_dispatch(b))return -1;
    return 0;
bad:free(scene);kflags_free(f);kcontrol_free(s);return -1;
}

int bootstrap_load_slot(KBootstrap *b,unsigned selector,unsigned slot){return b&&b->vm->byte_count==8192?load_runtime_from(b,bootstrap_save_dir(b),selector,slot):-1;}
int bootstrap_load_animation_switch(KBootstrap *b,unsigned selector){
    if(!b)return -1;
    char root[4096];
    if(snprintf(root,sizeof(root),"%s/kisaku-menu-resume",bootstrap_save_dir(b))>=(int)sizeof(root))return -1;
    /* Main story and each appendix keep their own internal checkpoint. */
    if(selector>3)return -1;
    return load_runtime_from(b,root,selector,1);
}

/* Native 4755f0 prepares replay from a SceneData checkpoint. The frontend keeps
   the original runtime frozen, so failed initialization cannot destroy it. */
int bootstrap_scene_replay_begin(KBootstrap *b,const KBootstrap *owner,unsigned slot){
    if(!b||!owner||b==owner||!b->title.active||b->error[0]||owner->error[0]||owner->scene_replay||strcmp(b->root,owner->root))return -1;
    KSceneHistory history=owner->scene_history;KSceneCheckpoint target;
    if((!history.loaded&&khistory_load(&history,bootstrap_save_dir(b)))||khistory_checkpoint(&history,slot,&target)||kscene_module(target.module)!=target.scene)return -1;
    int id=module(b,target.module);size_t offset;
    if(id<0||kvm_checkpoint_offset(b->vm,id,target.checkpoint,&offset))return -1;
    const KVM *source=owner->vm;KVM *v=b->vm;
    if(source->byte_count!=8192||source->word_count<381||source->global_count[0]<=50||source->global_count[1]<=69)return -1;
    uint8_t *raw=malloc(owner->raw_size?owner->raw_size:1);KScene *scene=owner->scene?malloc(sizeof(*scene)):NULL;
    if(!raw||(owner->scene&&!scene)){free(raw);free(scene);return -1;}
    if(owner->raw_size)memcpy(raw,owner->raw_variables,owner->raw_size);
    if(scene)*scene=*owner->scene;
    for(unsigned bank=0;bank<2;bank++){
        v->global_count[bank]=source->global_count[bank];
        for(unsigned i=0;i<v->global_count[bank];i++)if(owned_value(b,&v->globals[bank][i],source->globals[bank][i])){free(raw);free(scene);return -1;}
    }
    memcpy(v->bytes,source->bytes,sizeof(v->bytes));v->byte_count=source->byte_count;
    memcpy(v->words,source->words,sizeof(v->words));v->word_count=source->word_count;
    free(b->raw_variables);b->raw_variables=raw;b->raw_size=owner->raw_size;free(b->scene);b->scene=scene;
    memset(v->bytes,0,2000);v->bytes[2000]=(uint8_t)target.completion;v->bytes[4012]=v->bytes[4013]=v->bytes[8189]=1;
    v->globals[1][69]=(KValue){1,NULL};v->globals[1][54]=(KValue){0,NULL};v->globals[0][48]=(KValue){(int32_t)target.checkpoint,NULL};
    v->globals[0][50].number&=~0x100;
    b->font_width=owner->font_width;b->font_height=owner->font_height;
    b->scene_history=history;b->gallery=owner->gallery;b->gallery.dirty=0;
    if(b->read_size==owner->read_size&&b->read_flags&&owner->read_flags)memcpy(b->read_flags,owner->read_flags,b->read_size);
    b->read_selector=owner->read_selector;b->read_loaded=owner->read_loaded;b->read_dirty=0;
    b->title.active=0;b->scene_replay=1;b->scene_replay_finished=0;b->message_index=-1;
    if(kvm_start(v,id))return -1;
    v->ip=offset;return 0;
}
int bootstrap_scene_replay_merge(KBootstrap *owner,KBootstrap *replay){
    if(!owner||!replay||!replay->scene_replay||!replay->scene_replay_finished||replay->error[0]||strcmp(owner->root,replay->root))return -1;
    unsigned selector=owner->vm->bytes[8100];if(selector>3)selector=3;
    /* 475b76..475ba0 merges replay progress into FLAG0, then imports its
       persistent byte range after restoring the original script context. */
    if(bootstrap_flush_progress(replay)||kflags_progress(owner->root,selector,replay->vm->bytes,replay->vm->byte_count))return -1;
    KFlags *flags=kflags_read_slot(owner->root,selector,0);if(!flags)return -1;
    if(flags->byte_count<7000){kflags_free(flags);return -1;}
    memcpy(owner->vm->bytes+2000,flags->bytes+2000,5000);kflags_free(flags);
    owner->vm->bytes[2000]=owner->vm->bytes[4012]=0;owner->vm->bytes[4015]=owner->vm->bytes[4016]=1;
    owner->gallery=replay->gallery;
    if(owner->read_size==replay->read_size&&owner->read_flags&&replay->read_flags){
        for(size_t i=0;i<owner->read_size;i++)owner->read_flags[i]|=replay->read_flags[i];
        owner->read_dirty=1;
    }
    if(owner->scene){
        memcpy(owner->scene->status,owner->vm->bytes+4500,370);memcpy(owner->scene->flags,owner->vm->bytes+5000,370);
        /* Retained path and active red links belong to the original story. */
        for(unsigned i=0;i<1000;i++)if(!owner->scene->visited[i]&&owner->vm->bytes[3000+i])owner->scene->visited[i]=1;
    }
    return 0;
}

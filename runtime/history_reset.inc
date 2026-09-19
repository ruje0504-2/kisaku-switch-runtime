/* 4291c0 / 44ca80: New Game's four independent reset checkboxes.
   Original saves stay read-only. Commit all replacement files through one
   recoverable journal before changing the live VM or disabling checkboxes. */
static int reset_pack_flags(KBootstrap *b,const KFlags *flags,uint8_t **data,size_t *size){
    char path[4096];snprintf(path,sizeof(path),"%s/kisaku-reset-stage.flag",bootstrap_save_dir(b));
    if(kflags_write(flags,path))return -1;
    FILE *f=fopen(path,"rb");if(!f)return -1;
    int bad=fseek(f,0,SEEK_END);long length=bad?-1:ftell(f);
    if(length<0||length>33554432||fseek(f,0,SEEK_SET))bad=1;
    if(!bad){*size=(size_t)length;*data=malloc(*size?*size:1);if(!*data)bad=1;}
    if(!bad&&fread(*data,1,*size,f)!=*size)bad=1;
    if(fclose(f))bad=1;
    remove(path);return bad?-1:0;
}
int bootstrap_history_reset(KBootstrap *b,int confirm){
    if(!b||!b->flag_dialog.active||!b->extra_active||b->extra_kind!=15)return -1;
    if(!confirm){b->extra_active=b->extra_request=b->extra_kind=0;return 0;}
    KVM *v=b->vm;KFlagDialog *dialog=&b->flag_dialog;
    unsigned all=dialog->checked[3]==1,read=all||dialog->checked[0]==1,choice=all||dialog->checked[1]==1,nav=all||dialog->checked[2]==1;
    if(!read&&!choice&&!nav)return -1;
    if(v->byte_count!=8192||v->word_count<600||v->global_count[1]<100||b->raw_size<6000||!b->read_size)return error(b,"history reset state invalid");
    unsigned selector=v->bytes[8100];if(selector>3)return error(b,"history reset selector invalid");
    uint8_t bytes[8192];uint16_t words[8192];KValue bank1[8192];
    memcpy(bytes,v->bytes,sizeof(bytes));memcpy(words,v->words,v->word_count*sizeof(*words));memcpy(bank1,v->globals[1],v->global_count[1]*sizeof(*bank1));
    uint8_t *raw=malloc(b->raw_size),*reads=calloc(b->read_size,1),*flag_data=NULL,*empty_data=NULL;size_t flag_size=0,empty_size=0;
    if(!raw||!reads){free(raw);free(reads);return error(b,"history reset allocation failed");}memcpy(raw,b->raw_variables,b->raw_size);
    if(choice)memset(bytes+2001,0,799);
    KSceneHistory history=b->scene_history;
    if(nav){
        memset(bytes+3000,0,1000);memset(bytes+4500,0,370);memset(bytes+5000,0,370);memset(words+11,0,370*sizeof(*words));
        if(!history.loaded&&khistory_load(&history,bootstrap_save_dir(b)))goto bad;
        memset(history.data+4,0,2400+600);for(unsigned i=0;i<600;i++)history.data[3004+i*32]=0;
    }
    if(all){memset(bytes,0,8192);memset(words,0,600*sizeof(*words));memset(raw,0,6000);memset(bank1,0,100*sizeof(*bank1));}
    KFlags flags={0};
    if(v->module>=0)snprintf((char *)flags.module,sizeof(flags.module),"%s",v->modules[v->module].name);
    flags.globals[0]=v->globals[0];flags.counts[0]=v->global_count[0];flags.globals[1]=bank1;flags.counts[1]=v->global_count[1];
    flags.bytes=bytes;flags.byte_count=v->byte_count;flags.words=words;flags.word_count=v->word_count;flags.raw=raw;flags.raw_count=(unsigned)b->raw_size;
    if((choice||nav)&&reset_pack_flags(b,&flags,&flag_data,&flag_size))goto bad;
    if(all){flags.module[0]=0;if(reset_pack_flags(b,&flags,&empty_data,&empty_size))goto bad;}
    uint8_t gallery[KGALLERY_COUNT+1];
    if(all){
        if(kgallery_load(&b->gallery,&b->data,bootstrap_save_dir(b),selector))goto bad;
        memcpy(gallery,b->gallery.flags,sizeof(gallery));memset(gallery,0,1135);gallery[KGALLERY_COUNT]=0;
        for(unsigned i=1135;i<KGALLERY_COUNT;i++)if(gallery[i])gallery[KGALLERY_COUNT]=1;
    }
    KResetFile files[205];char names[205][64];unsigned count=0;
#define RESET_FILE(label,p,n) do{snprintf(names[count],sizeof(names[count]),"%s",label);files[count]=(KResetFile){names[count],p,n};count++;}while(0)
    char name[64];
    if(read){snprintf(name,sizeof(name),"kisaku-read-%u.dat",selector);RESET_FILE(name,reads,b->read_size);}
    if(choice||nav){snprintf(name,sizeof(name),"kisaku-flag-%u.dat",selector);RESET_FILE(name,flag_data,flag_size);}
    if(nav)RESET_FILE("kisaku-scene.dat",history.data,sizeof(history.data));
    if(all){
        snprintf(name,sizeof(name),"kisaku-cg-%u.dat",selector);RESET_FILE(name,gallery,sizeof(gallery));
        for(unsigned slot=1;slot<=100;slot++){
            snprintf(name,sizeof(name),"kisaku-flag-%u-%03u.dat",selector,slot);RESET_FILE(name,empty_data,empty_size);
            snprintf(name,sizeof(name),"kisaku-slot-%u-%03u.index",selector,slot);RESET_FILE(name,"KISAKU-SLOT-1 0\n",sizeof("KISAKU-SLOT-1 0\n")-1);
        }
    }
#undef RESET_FILE
    b->reset_pending=1;
    if(kreset_prepare(bootstrap_save_dir(b),files,count)||kreset_recover(bootstrap_save_dir(b)))goto bad;
    b->reset_pending=0;
    memcpy(v->bytes,bytes,sizeof(bytes));memcpy(v->words,words,v->word_count*sizeof(*words));memcpy(v->globals[1],bank1,v->global_count[1]*sizeof(*bank1));memcpy(b->raw_variables,raw,b->raw_size);
    if(read){memset(b->read_flags,0,b->read_size);b->read_loaded=1;b->read_selector=selector;b->read_dirty=0;}
    if(nav){b->scene_history=history;if(b->scene){memset(b->scene->visited,0,sizeof(b->scene->visited));memset(b->scene->status,0,sizeof(b->scene->status));memset(b->scene->flags,0,sizeof(b->scene->flags));memset(b->scene->counters,0,sizeof(b->scene->counters));b->scene->path_count=0;if(all)memset(b->scene->route_group,0,sizeof(b->scene->route_group));}}
    if(all){memcpy(b->gallery.flags,gallery,sizeof(gallery));b->gallery.dirty=0;}
    for(unsigned i=0;i<4;i++)if(all||dialog->checked[i]==1)dialog->checked[i]=2;
    b->extra_active=b->extra_request=b->extra_kind=0;
    free(raw);free(reads);free(flag_data);free(empty_data);return 0;
bad:free(raw);free(reads);free(flag_data);free(empty_data);return error(b,"history reset write failed; restart to recover pending reset");
}

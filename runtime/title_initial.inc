/* CTitle 4ed590: modes 0/2 main menus, modes 1/3 appendix menus.
   Disabled entries retain their atlas cell but cannot return a selection. */
/* Older portable saves did not propagate bank1[60] to FLAG100/201.
   Recover availability only from an actual readable checkpoint, never unlock
   a new installation or infer validity from a filename alone. */
static int title_has_saved_checkpoint(KBootstrap *b){
    unsigned selector=b->vm->globals[1][61].number==1;
    for(unsigned slot=1;slot<=100;slot++){
        KFlags *f=NULL;KControlStore *c=NULL;
        int valid=!kslot_read(bootstrap_save_dir(b),selector,slot,&f,&c);
        valid=valid&&f->byte_count==9192&&f->word_count==600&&f->raw_count==15000&&
            f->counts[1]==100&&!f->globals[1][61].string&&f->globals[1][61].number==(int)selector&&
            memchr(f->module,0,260)&&f->module[0]&&c->count;
        kflags_free(f);kcontrol_free(c);if(valid)return 1;
    }
    return 0;
}
static int title_layout_native(KBootstrap *b,unsigned mode,KTitle *t){
    KVM *v=b->vm;
    if(mode>3||v->global_count[1]<=60||v->globals[1][60].string||v->byte_count<=4009)
        return error(b,"CTitle mode or progress invalid");
    if((mode==0||mode==2)&&v->globals[1][60].number!=1&&title_has_saved_checkpoint(b))
        v->globals[1][60]=(KValue){1,NULL};
    t->variant=4;t->selected=-1;t->unlocked=v->globals[1][60].number==1;
    unsigned ids[6]={0,1,2,3,4,5};int enabled[6]={1,t->unlocked,1,t->unlocked,1,1};
    t->count=6;
    if(mode==0){if(!v->bytes[1058]){t->count=5;ids[4]=5;}}
    else if(mode==1){
        unsigned extra[]={6,7,8,9,13,10};memcpy(ids,extra,sizeof(ids));
        unsigned flags[]={4005,4002,3290,4004};
        for(unsigned i=0;i<4;i++)enabled[i]=v->bytes[flags[i]]==1;
    }else if(mode==2){
        enabled[2]=v->bytes[3600]!=0;t->count=3;
        if(v->bytes[4009]==1){ids[t->count]=11;enabled[t->count++]=1;}
        ids[t->count]=3;enabled[t->count++]=t->unlocked;
        ids[t->count]=12;enabled[t->count++]=1;
    }else{
        t->count=3;ids[0]=6;ids[1]=8;ids[2]=10;
        enabled[0]=v->bytes[3601]==1;enabled[1]=v->bytes[3290]==1;enabled[2]=1;
    }
    for(unsigned i=0;i<t->count;i++){t->native_sources[i]=ids[i];t->native_ids[i]=enabled[i]?(int)ids[i]:-1;}
    return 0;
}
static int title_initial(KBootstrap *b,unsigned mode){
    KTitle t={0};if(title_layout_native(b,mode,&t))return -1;
    t.background=b->layers[2];t.parts=b->layers[3];
    KImage dst=b->layers[1];
    if(dst.width<640||dst.height<480)return error(b,"CTitle initial surface too small");
    dst.width=640;dst.height=480;
    if(ktitle_draw(&t,&dst))return error(b,"CTitle initial surfaces invalid");
    return 0;
}

static int title_open_native(KBootstrap *b,unsigned mode){
    if(title_initial(b,mode))return -1;
    uint8_t *area=NULL;size_t size=0;
    if(read_named(&b->data,"title.area",&area,&size)||!size){free(area);return error(b,"CTitle area resource missing");}
    free(area);
    KTitle t={0};const KImage *sources[]={&b->layers[2],&b->layers[3]};KImage *dest[]={&t.background,&t.parts};
    for(unsigned i=0;i<2;i++){
        const KImage *src=sources[i];size_t stride=(size_t)src->width*4;
        uint8_t *pixels=malloc(stride*src->height);
        if(!pixels){ktitle_free(&t);return error(b,"CTitle surface allocation failed");}
        *dest[i]=(KImage){0,0,src->width,src->height,stride,pixels};
        for(unsigned y=0;y<src->height;y++)memcpy(pixels+y*stride,src->pixels+y*src->stride,stride);
    }
    if(title_layout_native(b,mode,&t)){ktitle_free(&t);return -1;}
    t.active=1;t.age=64;
    if(ktitle_draw(&t,&b->layers[0])){ktitle_free(&t);return error(b,"CTitle initial composition failed");}
    ktitle_free(&b->title);b->title=t;return 0;
}

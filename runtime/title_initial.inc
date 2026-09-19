/* CTitle 4ed590, mode 0. Action 0 prepares layer 1; the script presents it. */
static int title_initial(KBootstrap *b){
    KVM *v=b->vm;KImage *dst=&b->layers[1],*bg=&b->layers[2],*parts=&b->layers[3];
    if(!dst->pixels||!bg->pixels||!parts->pixels||dst->width<640||dst->height<480||
       bg->width<640||bg->height<480||parts->width<564||parts->height<256||
       v->global_count[1]<=60||v->globals[1][60].string||v->byte_count<=1058)
        return error(b,"CTitle initial surfaces or progress invalid");
    int unlocked=v->globals[1][60].number==1;
    unsigned x[6]={0,188,376,0,188,376},y[6]={0,unlocked?0:96,0,unlocked?128:224,128,128};
    unsigned count=v->bytes[1058]?6:5;
    if(count==5){x[4]=376;y[4]=128;}
    for(unsigned row=0;row<480;row++)memcpy(dst->pixels+row*dst->stride,bg->pixels+row*bg->stride,640*4);
    for(unsigned i=0;i<count;i++)for(unsigned row=0;row<32;row++)for(unsigned col=0;col<188;col++){
        const uint8_t *src=parts->pixels+(y[i]+row)*parts->stride+(x[i]+col)*4;
        uint8_t *out=dst->pixels+(260+i*36+row)*dst->stride+(452+col)*4;
        for(unsigned c=0;c<3;c++)out[c]=(uint8_t)(out[c]*(255-src[3])/255+src[c]*src[3]/255);
    }
    return 0;
}

static int title_open_native(KBootstrap *b){
    if(title_initial(b))return -1;
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
    t.variant=4;t.active=1;t.age=64;t.selected=-1;t.unlocked=b->vm->globals[1][60].number==1;
    t.count=b->vm->bytes[1058]?6:5;
    int ids[]={0,t.unlocked?1:-1,2,t.unlocked?3:-1,4,5};
    memcpy(t.native_ids,ids,sizeof(ids));if(t.count==5)t.native_ids[4]=5;
    if(ktitle_draw(&t,&b->layers[0])){ktitle_free(&t);return error(b,"CTitle initial composition failed");}
    ktitle_free(&b->title);b->title=t;return 0;
}

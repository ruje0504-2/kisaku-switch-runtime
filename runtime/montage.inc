/* 42cd60: s03d's 24-image accelerating montage. No script return value. */
static const char *montage_names[]={"ev05.rmt","ev13a.rmt","ev31.rmt","ev37.rmt","ev54.rmt","ev55.rmt","ev57.rmt","ev60.rmt","ev91.rmt","ev92.rmt","ev97.rmt","ev124.rmt","ev123.rmt","ev127.rmt","ev145.rmt","ev153a.rmt","ev160.rmt","ev180.rmt","ev206.rmt","ev223.rmt","ev242.rmt","ev242a.rmt","ev242b.rmt","ev242c.rmt"};
static int montage_present(KBootstrap *b){
    int32_t q[9]={0,0,640,480,0,0,0,2,1};if(blit_args(b,0,q))return -1;
    b->montage_presented=1;b->montage_clock=(b->montage_index<12?600-b->montage_index*50:0)*60;return 0;
}
static int montage_load(KBootstrap *b){
    const char *name=montage_names[b->montage_index];b->montage_presented=0;
    if(b->image_worker){
        if(kimage_worker_submit(b->image_worker,name))return error(b,"montage image submission failed");
        b->image_layer=2;b->image_offset_x=b->image_offset_y=0;b->image_loading=1;snprintf(b->image_name,sizeof(b->image_name),"%s",name);
    }else{
        uint8_t *data=NULL;size_t size=0;KImage image={0};
        if(read_named(&b->images,name,&data,&size)||rmt_decode(data,size,&image)){free(data);return error(b,"montage image decode failed");}free(data);
        int bad=install_image(b,2,&image,name);rmt_free(&image);if(bad)return -1;
        return montage_present(b);
    }
    return 0;
}
static int montage_begin(KBootstrap *b){
    b->montage_active=1;b->montage_index=0;b->montage_elapsed=b->montage_clock=0;return montage_load(b);
}
static void montage_frame(KBootstrap *b){
    b->montage_elapsed+=1000;if(b->image_loading)return;
    if(!b->montage_presented){montage_present(b);return;}
    if(b->montage_clock>1000){b->montage_clock-=1000;return;}
    b->montage_clock=0;
    if(b->montage_elapsed>480000||++b->montage_index==24){b->montage_active=b->montage_presented=0;return;}
    montage_load(b);
}

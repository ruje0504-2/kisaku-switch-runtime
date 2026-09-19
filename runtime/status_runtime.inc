/* 40c5b0: the separate native meter window uses meta.rmt and byte 300/301. */
static void status_copy(KImage *dst,const KImage *src,unsigned dx,unsigned dy,unsigned sx,unsigned sy,unsigned w,unsigned h){
    for(unsigned y=0;y<h;y++)memcpy(dst->pixels+(dy+y)*dst->stride+dx*4,src->pixels+(sy+y)*src->stride+sx*4,w*4);
}
static int status_refresh(KBootstrap *b){
    if(!b->status_parts.pixels){
        uint8_t *data=NULL;size_t n=0;if(read_named(&b->images,"meta.rmt",&data,&n)||rmt_decode(data,n,&b->status_parts)){free(data);return error(b,"status meter resource");}free(data);
        if(b->status_parts.width<218||b->status_parts.height<240)return error(b,"status meter dimensions");
        b->status_image=(KImage){0,0,116,240,464,calloc(116*240,4)};if(!b->status_image.pixels)return error(b,"status meter allocation");
    }
    status_copy(&b->status_image,&b->status_parts,0,0,0,0,116,240);
    for(unsigned i=0;i<2;i++){
        unsigned value=b->vm->bytes[300+i];if(value>100)value=100;
        status_copy(&b->status_image,&b->status_parts,12+i*58,12,184,0,34,200);
        if(value)status_copy(&b->status_image,&b->status_parts,12+i*58,212-value*2,116+i*34,200-value*2,34,value*2);
    }
    b->status_serial++;return 0;
}

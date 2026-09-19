/* 481df0: prepare CMesWnd sprites; this does not show or clear text. */
static int message_skin_reload(KBootstrap *b){
    KMessageSkin next={0};uint8_t *data=NULL;size_t size=0;
    if(read_named(&b->images,"kisaku_DL_mes_p.akb",&data,&size)||rmt_decode(data,size,&next.atlas)){
        free(data);return error(b,"CMesWnd skin decode failed");
    }
    free(data);
    if(next.atlas.width<608||next.atlas.height<164){kmessage_skin_free(&next);return error(b,"CMesWnd skin atlas too small");}
    const unsigned sx[]={76,532,152,76,0,152},sy[]={148,84,84,84,84,148};
    for(unsigned i=0;i<6;i++){
        unsigned w=i==5?57:76;uint8_t *pixels=malloc(w*16*4);
        if(!pixels){kmessage_skin_free(&next);return error(b,"CMesWnd button allocation failed");}
        next.buttons[i]=(KImage){i==5?0:564-(int)i*68,464,w,16,w*4,pixels};
        for(unsigned y=0;y<16;y++)memcpy(pixels+y*w*4,next.atlas.pixels+(sy[i]+y)*next.atlas.stride+sx[i]*4,w*4);
    }
    uint8_t color[4];const char *keys[]={"Blue","Green","Red","Alpha"};
    for(unsigned c=0;c<4;c++){
        int64_t value=option(b,"Msg",keys[c],c==3?92:0);
        if(c==3)value=224-value;
        if(value<0)value=0;
        if(value>224)value=224;
        value=value*255/184;if(value>255)value=255;color[c]=(uint8_t)value;
    }
    uint8_t *pixels=malloc(640*84*4);
    if(!pixels){kmessage_skin_free(&next);return error(b,"CMesWnd background allocation failed");}
    next.background=(KImage){0,396,640,84,2560,pixels};
    for(unsigned i=0;i<640*84;i++)memcpy(pixels+i*4,color,4);
    kmessage_skin_free(&b->message_skin);b->message_skin=next;return 0;
}

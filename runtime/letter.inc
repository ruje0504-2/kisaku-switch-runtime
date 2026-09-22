/* Kisaku CLetter +ec/+f0/+f4. Separate from inherited novel surfaces. */
static int letter_prepare(KBootstrap *b){
    KImage *screen=&b->layers[0],*text=&b->layers[1];
    if(b->letter_transition||b->letter_active||!screen->pixels||screen->width!=640||screen->height!=480||
       !text->pixels||text->width<640||text->height<480)
        return error(b,"CLetter requires idle 640x480 surfaces (arguments preserved)");
    KImage images[3]={{0}};
    for(unsigned i=0;i<3;i++)if(!b->letter_surfaces[i].pixels){
        images[i]=(KImage){0,0,640,480,2560,calloc(480,2560)};
        if(!images[i].pixels){
            for(unsigned j=0;j<3;j++)rmt_free(&images[j]);
            return error(b,"CLetter surface allocation failed (arguments preserved)");
        }
    }
    for(unsigned i=0;i<3;i++)if(images[i].pixels)b->letter_surfaces[i]=images[i];
    return 0;
}
static void letter_region(KBootstrap *b){
    /* 48a0a0: this differs from the inherited narrative window. */
    const unsigned slots[]={42,43,44,45,30,31,46,47,49};
    const int values[]={32,12,608,480,24,24,32,12,1};
    for(unsigned i=0;i<9;i++)b->vm->globals[0][slots[i]]=(KValue){values[i],NULL};
    for(unsigned y=0;y<480;y++)memset(b->layers[1].pixels+y*b->layers[1].stride,0,2560);
    b->message_pending_size=0;b->message_pending[0]=0;
    b->message_cursor_x=0;b->message_cursor_y=12;b->message_initialized=1;
    b->letter_cursor_x=0;b->letter_cursor_y=12;present_page_reset(b);
}
static void letter_darken(KImage *to,const KImage *from){
    /* 48cb10 -> 48b130: saturating subtraction of 90, leaving target Alpha. */
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *s=from->pixels+y*from->stride+x*4;
        uint8_t *d=to->pixels+y*to->stride+x*4;
        for(unsigned c=0;c<3;c++)d[c]=s[c]>90?s[c]-90:0;
    }
}
static int letter_finish(KBootstrap *b){
    unsigned transition=b->letter_transition;b->letter_transition=0;present_fade_release(b);
    if(transition==1){
        if(message_init(b))return -1;
        b->letter_mode=0;b->novel_mode=0;
    }else if(transition==2)b->letter_mode=1;
    else if(transition==4){b->message_user_hidden=1;if(b->letter_backlog)b->message_request=5;}
    else if(transition==5)b->message_user_hidden=0;
    else if(transition==3)memcpy(b->letter_surfaces[0].pixels,b->letter_surfaces[2].pixels,480*2560);
    if(transition==1)present_page_free(b);
    else if(transition==2||transition==3){present_page_background(b,&b->layers[0]);present_page_snapshot(b);}
    else if(transition==4)b->present_page_active=0;
    else if(transition==5)present_page_all(b);
    return 0;
}
static int letter_render(KBootstrap *b){
    KImage *screen=&b->layers[0],*from=&b->letter_surfaces[0],*to=&b->letter_surfaces[1];
    unsigned alpha=b->letter_alpha;
    for(unsigned y=0;y<480;y++){
        uint8_t *out=screen->pixels+y*screen->stride;
        const uint8_t *src=from->pixels+y*from->stride,*dst=to->pixels+y*to->stride;
        if(alpha>=255){memcpy(out,dst,640*4);continue;}
        for(unsigned x=0;x<640;x++){
            for(unsigned c=0;c<3;c++)out[x*4+c]=(uint8_t)(src[x*4+c]*(255-alpha)/255+dst[x*4+c]*alpha/255);
            out[x*4+3]=src[x*4+3];
        }
    }
    if(alpha<255){
        if(b->letter_transition==5){
            if(b->present_page_valid&&!present_fade_prepare(b)){
                memcpy(b->present_fade_from.pixels,from->pixels,640*480*4);
                present_fade_frame(b,&b->present_page_base,alpha,1);
            }
        }else present_fade_frame(b,to,alpha,0);
    }
    return alpha>=255?letter_finish(b):0;
}
static int letter_begin(KBootstrap *b,unsigned transition){
    int speed=option(b,"Display","EffectSpeed",0);
    b->letter_increment=speed==1?64:speed==2?256:32;
    b->letter_alpha=b->letter_increment;b->letter_transition=transition;b->letter_clock=0;
    return letter_render(b);
}
static int letter_call(KBootstrap *b,int action){
    if(action!=0&&action!=2&&action!=3)
        return error(b,"CLetter action unsupported (arguments preserved)");
    if(action!=2&&(b->message_visible||b->message_active||b->novel_mode))
        return error(b,"CLetter entry/clear requires hidden message window (arguments preserved)");
    if(action==3&&!b->letter_mode)
        return error(b,"CLetter clear before entry (arguments preserved)");
    if(letter_prepare(b))return -1;
    KImage *screen=&b->layers[0],*saved=&b->letter_surfaces[0],*target=&b->letter_surfaces[1];
    if(action==0){ /* 4fccc0 -> 48aed0 */
        for(unsigned y=0;y<480;y++)memcpy(saved->pixels+y*2560,screen->pixels+y*screen->stride,2560);
        letter_darken(target,screen);b->font_width=b->font_height=24;letter_region(b);
        return letter_begin(b,2);
    }
    present_fade_capture(b);
    if(action==3){ /* 48acd0 preserves the original background across clear. */
        letter_region(b);
        memcpy(b->letter_surfaces[2].pixels,saved->pixels,480*2560);
        letter_darken(target,saved);
        for(unsigned y=0;y<480;y++)memcpy(saved->pixels+y*2560,screen->pixels+y*screen->stride,2560);
        return letter_begin(b,3);
    }
    /* 48adf0: copy saved background before replacing it with the screen. */
    memcpy(target->pixels,saved->pixels,480*2560);
    for(unsigned y=0;y<480;y++)memcpy(saved->pixels+y*2560,screen->pixels+y*screen->stride,2560);
    b->font_width=b->font_height=16;
    return letter_begin(b,1);
}
static void letter_frame(KBootstrap *b){
    /* 48a960 waits 15ms after each displayed interpolation step. */
    b->letter_clock+=1000;
    while(b->letter_clock>=900&&b->letter_transition){
        b->letter_clock-=900;b->letter_alpha+=b->letter_increment;
        if(letter_render(b))return;
    }
}

/* 489760/489aa0: composite from the captured screen, never from the last
   blended frame. Keep layer1 Alpha intact; the native saves/restores it. */
static void letter_text_line(KBootstrap *b,int complete){
    KImage *screen=&b->layers[0],*text=&b->layers[1],*base=&b->letter_surfaces[2];
    int line=b->vm->globals[0][31].number;
    for(int y=b->letter_reveal_y;y<b->letter_reveal_y+line;y++)for(int x=b->letter_reveal_x;x<640;x++){
        const uint8_t *src=text->pixels+y*text->stride+x*4,*bg=base->pixels+y*base->stride+x*4;
        uint8_t *dst=screen->pixels+y*screen->stride+x*4;
        int alpha=src[3];
        if(!complete){int ramp=(int)b->letter_phase-b->letter_mask[y*640+x];if(ramp<0)ramp=0;if(alpha>ramp)alpha=ramp;}
        for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*alpha/255+bg[c]*(255-alpha)/255);
    }
    present_page_line(b,b->letter_reveal_x,b->letter_reveal_y,line,complete?NULL:b->letter_mask,(int)b->letter_phase,255);
    present_page_snapshot(b);
}
static void letter_text_all(KBootstrap *b){
    KImage *screen=&b->layers[0],*base=&b->letter_surfaces[1],*text=&b->layers[1];
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *bg=base->pixels+y*base->stride+x*4,*src=text->pixels+y*text->stride+x*4;
        uint8_t *dst=screen->pixels+y*screen->stride+x*4;unsigned alpha=src[3];
        for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*alpha/255+bg[c]*(255-alpha)/255);
    }
    b->message_revealing=0;present_page_all(b);
}
static void letter_text_confirm(KBootstrap *b){
    if(b->letter_transition||b->letter_exit_pending||!b->letter_active||b->message_user_hidden)return;
    if(kvm_push(b->vm,(KValue){0,NULL})){error(b,"CLetter result allocation failed");return;}
    letter_text_all(b);message_skip_voice(b);
    b->letter_cursor_x=b->vm->globals[0][46].number;b->letter_cursor_y=b->vm->globals[0][47].number;
    b->message_active=b->letter_active=0;
    b->vm->globals[0][50].number|=0x80; /* 48afa0 restores recording. */
    b->input_events=0;
}
static int letter_text_begin(KBootstrap *b,int id){
    KVM *v=b->vm;
    if(v->byte_count>4010&&v->bytes[4010]==1)id=27680;
    int line=v->globals[0][31].number,end=v->globals[0][47].number;
    if(!b->letter_mode||b->message_active||b->message_user_hidden||
       (id>=0&&(!b->read_flags||(size_t)id>=b->read_size*8))||
       v->globals[0][49].number!=1||line!=24||
       b->letter_cursor_x<0||b->letter_cursor_x>640||b->letter_cursor_y<0||
       end<b->letter_cursor_y||end>480-line||(end-b->letter_cursor_y)%line||
       v->globals[0][46].number<0||v->globals[0][46].number>640)
        return error(b,"CLetter text region/read id invalid (arguments preserved)");
    if(letter_prepare(b))return -1;
    if(!b->letter_mask){
        uint8_t *mask=NULL;size_t size=0;
        if(read_named(&b->data,"01.bin",&mask,&size)||size<640*480){free(mask);return error(b,"CLetter 01.bin missing/truncated (arguments preserved)");}
        b->letter_mask=mask;
    }
    if(b->audio_counts[2]!=1)return error(b,"CLetter voice channel missing (arguments preserved)");
    b->message_read_id=id;b->message_was_read=id<0||(b->read_flags[(unsigned)id>>3]&(0x80u>>(id&7)))!=0;
    b->message_had_voice=option(b,"Msg","IsAutoMes",0)&&b->audio_objects[2][0].state;
    size_t bytes=b->message_pending_size?b->message_pending_size:v->text_size;
    int speed=option(b,"Msg","AutoMesSpeed",52);if(speed<0)speed=0;if(speed>104)speed=104;
    b->message_auto_delay=option(b,"Msg","IsAutoMes",0)?500+(unsigned)((int)((104-speed)*1.7))*(unsigned)(bytes/2):0;
    b->message_auto_clock=0;
    if(b->force_skip||(option(b,"Msg","IsOneMes",0)&&b->message_was_read))message_skip_voice(b);
    else if(voice_play(b))return -1;
    message_history_record(b);b->message_pending_size=0;b->message_pending[0]=0;
    b->message_active=b->letter_active=1;b->message_revealing=1;
    b->message_user_hidden=b->message_timed=b->message_open=b->message_buttons_motion=b->message_request=0;
    b->message_hover=-1;
    b->letter_reveal_x=b->letter_cursor_x;b->letter_reveal_y=b->letter_cursor_y;b->letter_end_y=end+line;
    speed=option(b,"Display","EffectSpeed",0);
    b->letter_text_increment=speed==1?64:32;b->letter_phase=b->letter_text_increment;b->letter_reveal_clock=0;
    for(unsigned y=0;y<480;y++)memcpy(b->letter_surfaces[2].pixels+y*2560,b->layers[0].pixels+y*b->layers[0].stride,2560);
    v->globals[0][50].number&=~0x80;
    if(id>=0)mark_read(b,(unsigned)id);
    if(speed==2)letter_text_all(b);
    return 0;
}
static void letter_text_hide(KBootstrap *b,int hidden){
    if(!b->letter_active||b->letter_transition||b->letter_exit_pending||b->message_user_hidden==(unsigned)(hidden!=0))return;
    KImage *screen=&b->layers[0],*saved=&b->letter_surfaces[0],*target=&b->letter_surfaces[1],*text=&b->layers[1];
    if(hidden){ /* 48abe0, after 489d00 completes the reveal. */
        letter_text_all(b);present_fade_capture(b);memcpy(target->pixels,saved->pixels,480*2560);
        for(unsigned y=0;y<480;y++)memcpy(saved->pixels+y*2560,screen->pixels+y*screen->stride,2560);
        b->font_width=b->font_height=16;b->vm->globals[0][30].number=16;b->vm->globals[0][31].number=18;
        (void)letter_begin(b,4);
    }else{ /* 48aab0: restore original background, Mincho metrics and text. */
        b->font_width=b->font_height=24;b->vm->globals[0][30].number=b->vm->globals[0][31].number=24;
        for(unsigned y=0;y<480;y++)memcpy(saved->pixels+y*2560,screen->pixels+y*screen->stride,2560);
        letter_darken(target,screen);
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
            const uint8_t *src=text->pixels+y*text->stride+x*4;uint8_t *dst=target->pixels+y*target->stride+x*4;unsigned a=src[3];
            for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*a/255+dst[c]*(255-a)/255);
        }
        (void)letter_begin(b,5);
    }
}
static void letter_text_frame(KBootstrap *b){
    if(!b->letter_active||b->letter_transition||b->message_user_hidden||b->message_request)return;
    if(b->message_revealing){
        b->letter_reveal_clock+=1000;
        while(b->message_revealing&&b->letter_reveal_clock>=900){
            b->letter_reveal_clock-=900;letter_text_line(b,0);b->letter_phase+=b->letter_text_increment;
            if(b->letter_phase>383){
                letter_text_line(b,1);b->letter_reveal_y+=24;b->letter_reveal_x=0;
                if(b->letter_reveal_y>=b->letter_end_y)b->message_revealing=0;
                else {b->letter_phase=b->letter_text_increment;for(unsigned y=0;y<480;y++)memcpy(b->letter_surfaces[2].pixels+y*2560,b->layers[0].pixels+y*b->layers[0].stride,2560);}
            }
        }
    }
    if(b->force_skip||(option(b,"Msg","IsOneMes",0)&&b->message_was_read))letter_text_confirm(b);
    else if(option(b,"Msg","IsAutoMes",0)&&
       (b->message_had_voice?(!b->voice_loading&&!b->voice_active):
       (!b->message_revealing&&b->message_auto_clock>(uint64_t)b->message_auto_delay*60)))letter_text_confirm(b);
}

/* 48a8b0 -> 4b3ba0: after the hidden-page confirmation, unwind the modal
   and start the scene selector script, without returning into its caller. */
int bootstrap_letter_exit(KBootstrap *b,int confirm){
    if(!b||!b->letter_active||!b->letter_exit_pending||!b->message_user_hidden||b->letter_transition)return -1;
    if(!confirm){b->letter_exit_pending=0;b->message_request=0;letter_text_hide(b,0);return 0;}
    KVM *v=b->vm;
    if(v->global_count[1]<=61||v->globals[1][61].string)return -1;
    const char *name=v->globals[1][61].number?"hage_scmode.mes":"scene.mes";
    /* A missing/malformed selector must not destroy the waiting dialogue. */
    KStatus status=v->status;char vm_error[sizeof(v->error)],runtime_error[sizeof(b->error)];
    memcpy(vm_error,v->error,sizeof(vm_error));memcpy(runtime_error,b->error,sizeof(runtime_error));
    int id=module(b,name);
    if(id<0){v->status=status;memcpy(v->error,vm_error,sizeof(vm_error));memcpy(b->error,runtime_error,sizeof(runtime_error));return -1;}
    if(kvm_start(v,id))return -1;
    b->letter_exit_pending=b->letter_active=b->message_active=b->message_request=0;
    b->input_events=0;
    return 0;
}

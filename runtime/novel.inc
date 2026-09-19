/* AI6WIN 42bac0 / 440d50 / 441040: full-screen narrative text.
 * Background darkening is saturating RGB subtraction (43ff30), not opacity.
 * 442540 reveals each line through the original 01.bin alpha thresholds.
 */
static void novel_region(KBootstrap *b){
    const unsigned slots[]={42,43,44,45,46,47,30,31,49};
    const int values[]={32,32,608,480,32,32,24,26,1};
    for(unsigned i=0;i<9;i++)b->vm->globals[0][slots[i]]=(KValue){values[i],NULL};
    b->font_width=b->font_height=24;
    memset(b->layers[1].pixels,0,640*480*4);
    b->message_pending_size=0;b->message_pending[0]=0;
    b->novel_start_x=b->novel_start_y=32;
}
static void novel_fade_frame(KBootstrap *b){
    if(b->novel_text_reveal&&b->novel_step<255){
        /* Native alpha uses 0..128. Convert the threshold ramp to our
           0..255 bus, preserving the glyph coverage and preceding lines. */
        memcpy(b->layers[0].pixels,b->novel_from.pixels,640*480*4);
        for(int y=b->novel_start_y;y<b->novel_end_y;y++){
            int x0=y<b->novel_start_y+26?b->novel_start_x:0;
            for(int x=x0;x<640;x++){
                size_t at=(size_t)y*640+(unsigned)x,i=at*4;
                if(y<b->novel_reveal_y){memcpy(b->layers[0].pixels+i,b->novel_target.pixels+i,4);continue;}
                if(y>=b->novel_reveal_y+26)continue;
                int ramp=(int)b->novel_mask_phase-b->novel_mask[at];if(ramp<0)ramp=0;if(ramp>128)ramp=128;
                unsigned a=(unsigned)ramp*255/128;if(a>b->layers[1].pixels[i+3])a=b->layers[1].pixels[i+3];
                for(unsigned c=0;c<3;c++)b->layers[0].pixels[i+c]=(uint8_t)((b->layers[1].pixels[i+c]*a+b->novel_from.pixels[i+c]*(255-a))/255);
            }
        }
        return;
    }
    unsigned alpha=b->novel_step;
    if(alpha>255)alpha=255;
    for(size_t i=0;i<640*480*4;i+=4){
        for(unsigned c=0;c<3;c++)b->layers[0].pixels[i+c]=(uint8_t)(b->novel_from.pixels[i+c]*(255-alpha)/255+b->novel_target.pixels[i+c]*alpha/255);
        b->layers[0].pixels[i+3]=b->novel_target.pixels[i+3];
    }
    if(alpha==255){b->novel_transition=0;b->novel_text_reveal=0;b->message_revealing=0;}
}
static void novel_fade(KBootstrap *b,unsigned text){
    memcpy(b->novel_from.pixels,b->layers[0].pixels,640*480*4);
    int speed=option(b,"Display","EffectSpeed",0);
    b->novel_increment=speed==1?32:text?8:16;
    b->novel_step=(speed==2||(b->vm->globals[0][50].number&0x4000))?255:b->novel_increment;
    b->novel_text_reveal=text;
    b->novel_reveal_y=b->novel_start_y;b->novel_end_y=b->vm->globals[0][47].number+26;
    b->novel_mask_phase=b->novel_increment;
    if(text&&b->novel_step<255)b->novel_step=0;
    b->novel_transition=1;
    novel_fade_frame(b);
}
static void novel_frame(KBootstrap *b){
    if(b->novel_text_reveal){
        b->novel_mask_phase+=b->novel_increment;
        if(b->novel_mask_phase>=384){
            b->novel_reveal_y+=26;b->novel_mask_phase=b->novel_increment;
            if(b->novel_reveal_y>=b->novel_end_y)b->novel_step=255;
        }
    }else b->novel_step+=b->novel_increment;
    novel_fade_frame(b);
}
static int novel_dispatch(KBootstrap *b){
    int32_t action,id;
    if(integer(b,&action)||action<0||action>3)return error(b,"novel action invalid");
    if(!b->layers[0].pixels||!b->layers[1].pixels||b->layers[0].width!=640||b->layers[0].height!=480||b->layers[1].width!=640||b->layers[1].height!=480)return error(b,"novel surfaces invalid");
    if(!b->novel_mask){
        uint8_t *mask=NULL;size_t size=0;
        if(read_named(&b->data,"01.bin",&mask,&size)||size<640*480){free(mask);return error(b,"novel reveal mask invalid");}
        b->novel_mask=mask;
    }
    KImage *images[]={&b->novel_original,&b->novel_background,&b->novel_from,&b->novel_target};
    for(unsigned i=0;i<4;i++)if(!images[i]->pixels){
        uint8_t *p=calloc(640*480,4);if(!p)return error(b,"novel surface allocation failed");
        *images[i]=(KImage){0,0,640,480,2560,p};
    }
    if(!action){
        if(b->novel_mode||b->message_visible)return error(b,"novel entry requires hidden message box");
        memcpy(b->novel_original.pixels,b->layers[0].pixels,640*480*4);
        memcpy(b->novel_background.pixels,b->layers[0].pixels,640*480*4);
        for(size_t i=0;i<640*480*4;i+=4)for(unsigned c=0;c<3;c++){
            unsigned value=b->novel_background.pixels[i+c];b->novel_background.pixels[i+c]=value>90?value-90:0;
        }
        novel_region(b);b->novel_mode=1;
        memcpy(b->novel_target.pixels,b->novel_background.pixels,640*480*4);novel_fade(b,0);
    }else{
        if(!b->novel_mode)return error(b,"novel mode not initialized");
        if(action==1){
            if(integer(b,&id)||id<0||(size_t)id>=b->read_size*8)return error(b,"novel read id invalid");
            if(b->novel_start_x<0||b->novel_start_x>640||b->novel_start_y<0||b->vm->globals[0][47].number<b->novel_start_y||b->vm->globals[0][47].number+26>480)return error(b,"novel reveal bounds invalid");
            message_history_record(b);b->message_pending_size=0;b->message_pending[0]=0;
            b->message_auto_clock=0;
            int speed=option(b,"Msg","AutoMesSpeed",128);if(speed<0)speed=0;if(speed>255)speed=255;
            b->message_auto_delay=500+(unsigned)((int)((255-speed)*1.7))*(unsigned)(b->vm->text_size/2);
            b->message_had_voice=b->audio_counts[2]&&b->audio_objects[2][0].state;
            b->message_was_read=(b->read_flags[(unsigned)id>>3]&(0x80u>>(id&7)))!=0;
            if(!b->message_was_read)setting_put(b,"Msg","IsOneMes","0");
            memcpy(b->novel_target.pixels,b->novel_background.pixels,640*480*4);
            for(size_t i=0;i<640*480*4;i+=4){unsigned alpha=b->layers[1].pixels[i+3];for(unsigned c=0;c<3;c++)b->novel_target.pixels[i+c]=(uint8_t)(b->layers[1].pixels[i+c]*alpha/255+b->novel_target.pixels[i+c]*(255-alpha)/255);}
            b->message_user_hidden=0;b->message_active=1;b->message_revealing=1;b->message_keep_on_confirm=1;b->message_read_id=id;
            mark_read(b,(unsigned)id);
            if(voice_play(b))return -1;
            novel_fade(b,1);
        }else{
            if(action==2){
                memcpy(b->novel_target.pixels,b->novel_original.pixels,640*480*4);
                if(message_init(b))return -1;
                b->novel_mode=0;
            }else{novel_region(b);memcpy(b->novel_target.pixels,b->novel_background.pixels,640*480*4);}
            novel_fade(b,0);
        }
    }
    return 0;
}

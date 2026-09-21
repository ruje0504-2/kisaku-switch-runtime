/* Kisaku CConfig: 4c47c0, 4c6e00 and config_00/01/02/01_2.area.
   Changes remain a draft until 決定; no reference-game VM byte writes. */
/* 4c8800/4c82f0/4c69e0/4c67c0: 24 native steps, transformed by
   46bbe0 to 12/6/0; flag4000 keeps 24. Each step has a 15ms clock. */
static void config_motion_tick(MessagePanel *p,uint64_t now){
    while(p->config_motion){
        uint64_t elapsed=now>=p->config_clock?now-p->config_clock:0;
        if(p->config_steps&&elapsed<(uint64_t)p->config_steps*15){p->config_step=(unsigned)(elapsed/15);return;}
        p->config_clock+=(uint64_t)p->config_steps*15;p->config_step=0;
        unsigned motion=p->config_motion;p->config_motion=0;
        if(motion==2){p->setting_page=p->config_target;p->config_motion=3;}
        else if(motion==4){p->kind=p->config_next_kind;p->setting_ready=0;p->dismiss_menus=p->kind==0;p->viewing=p->back=0;}
        else if(motion==6)p->setting_page=1;
    }
}
static void config_motion_begin(MessagePanel *p,KBootstrap *b,unsigned motion){
    int speed=bootstrap_message_setting(b,8,0);
    p->config_steps=b->vm->globals[0][50].number&0x4000?24:speed==2?0:speed==1?6:12;
    p->config_motion=motion;p->config_step=0;p->config_clock=SDL_GetTicks64();
    p->config_hover=-1;p->selected=~0u;p->config_mouse_drag=0;
    kconfig_audio_stop(&p->config_audio);config_motion_tick(p,p->config_clock);
}
static void config_motion_skip(MessagePanel *p,KBootstrap *b){
    /* 46bca0 checks 102/103 (Shift/Ctrl), not confirm 100. */
    if(p->config_motion&&!(b->vm->globals[0][50].number&0x4000)){
        config_motion_tick(p,p->config_clock+(uint64_t)p->config_steps*30);
    }
}
static void settings_panel_init(MessagePanel *p,KBootstrap *b){
    if(p->setting_ready)return;
    for(unsigned i=0;i<KSET_COUNT;i++)p->setting_values[i]=bootstrap_message_setting(b,i,0);
    p->setting_ready=1;p->setting_page=(unsigned)p->setting_values[KSET_CONFIG_PAGE];
    if(p->setting_page>2)p->setting_page=1;
    p->selected=~0u;p->config_hover=-1;p->config_mouse_drag=0;p->native_kind=0;p->status[0]=0;
    config_motion_begin(p,b,1);
}
static int config_assets(MessagePanel *p,KBootstrap *b){
    const char *names[]={"kisaku_DL_text_p.akb","kisaku_DL_sound_p.akb","kisaku_DL_display_p.akb","kisaku_DL_sound_p2.akb","kisaku_DL_sound_p3.akb"};
    const unsigned sizes[][2]={{584,476},{264,152},{440,240},{336,340},{428,436}};
    for(unsigned i=0;i<5;i++)if(ui_asset(b,names[i],&p->config_art[i])||p->config_art[i].width<sizes[i][0]||p->config_art[i].height<sizes[i][1])return -1;
    const char *areas[]={"config_00.area","config_01.area","config_02.area","config_01_2.area"};
    for(unsigned page=0;page<4;page++)if(!p->config_area_count[page]){
        uint8_t *data=NULL;size_t size=0;
        if(ai6_read_named(&b->data,areas[page],&data,&size)){free(data);return -1;}
        unsigned count=size>=4?(unsigned)(data[0]|(unsigned)data[1]<<8|(unsigned)data[2]<<16|(unsigned)data[3]<<24):0;
        if(!count||count>68||size!=4+(size_t)count*20){free(data);return -1;}
        for(unsigned i=0;i<count;i++){
            int v[5];for(unsigned j=0;j<5;j++){const uint8_t *q=data+4+i*20+j*4;v[j]=(int32_t)(q[0]|(uint32_t)q[1]<<8|(uint32_t)q[2]<<16|(uint32_t)q[3]<<24);}
            if(v[0]<0||v[1]<0||v[2]<0||v[3]>640||v[4]>480||v[1]>=v[3]||v[2]>=v[4]){free(data);return -1;}
            p->config_areas[page][i]=(ConfigArea){v[0],v[1],v[2],v[3],v[4]};
        }
        free(data);p->config_area_count[page]=count;
    }
    return 0;
}
static int config_item(int id){
    int group=id>>8,part=id&255;
    if(group==0){const int items[]={11,12,13,9};return part<4?items[part]:-1;}
    if(group==1)return 0;
    if(group==2)return 16;
    if(group==3)return part==2?1:17;
    if(group>=4&&group<=7){const int toggles[]={6,7,5,19},volumes[]={3,4,2,KSET_H_VOLUME};return part==2?volumes[group-4]:toggles[group-4];}
    if(group==8)return KSET_SUNGEKI_SE;
    if(group==10)return part<3?8:part<5?21:-1;
    if(group==11)return KSET_MOVIE_VSYNC;
    if(group==16)return KSET_SUNGEKI_ANIME;
    if(group==17)return KSET_SCHEDULE_CHECK;
    if(group==18)return KSET_MINIGAME_DIFFICULTY;
    if(group>=100&&group<133)return KSET_CHARACTER_VOICE+group-100;
    return -1;
}
static int config_slider(int id){return (id>>8)<=1||((id&255)==2&&(id>>8)>=3&&(id>>8)<=7);}
static int config_enabled(MessagePanel *p,KBootstrap *b,int id){
    int group=id>>8,part=id&255,alternate=b->vm->globals[0][61].number==1;
    if(group==15){
        if(part<3)return 0; /* Native CConfig only shows the last three sidebar sprites. */
        if(part==3)return b->history_count&&(b->vm->globals[0][50].number&4)&&!(b->vm->globals[0][50].number&0x400)&&b->vm->globals[0][62].number!=1;
        if(part==4)return (b->vm->globals[0][50].number&4)!=0;
    }
    if(alternate&&(group==6||group==7||group==9||group==11||group==16||group==17||group==18||(group==10&&part>=3)))return 0;
    if(group==10&&part>=3&&b->vm->byte_count>4007&&b->vm->bytes[4007])return 0;
    if(config_slider(id)&&(group>=3&&group<=7)){
        const unsigned toggle[]={17,6,7,5,19};return p->setting_values[toggle[group-3]]!=0;
    }
    return 1;
}
static void config_page(MessagePanel *p,unsigned page){
    kconfig_audio_stop(&p->config_audio);
    p->config_motion=0;p->setting_page=page;p->selected=~0u;p->config_hover=-1;p->status[0]=0;
}
static void config_switch(MessagePanel *p,KBootstrap *b,unsigned page){
    if(page==p->setting_page)return;
    p->status[0]=0;
    if(page==3){config_page(p,3);config_motion_begin(p,b,5);}
    else if(p->setting_page==3)config_motion_begin(p,b,6);
    else {p->config_target=page;config_motion_begin(p,b,2);}
}
static void config_close(MessagePanel *p,KBootstrap *b,int commit){
    unsigned page=p->setting_page==3?1:p->setting_page;
    p->setting_values[KSET_CONFIG_PAGE]=(int)page;
    int failed=commit?bootstrap_settings_apply(b,p->setting_values,KSET_COUNT):
        bootstrap_message_setting(b,KSET_CONFIG_PAGE,(int)page-bootstrap_message_setting(b,KSET_CONFIG_PAGE,0))<0;
    if(failed){snprintf(p->status,sizeof(p->status),"設定を保存できませんでした");return;}
    kconfig_audio_stop(&p->config_audio);
    p->config_next_kind=0;b->message_hover=-1;config_motion_begin(p,b,4);
}
static void config_activate(MessagePanel *p,KBootstrap *b,int id){
    if(p->config_motion)return;
    if(id<0||!config_enabled(p,b,id))return;
    int group=id>>8,part=id&255,item=config_item(id);
    if(item>=0&&!config_slider(id))p->setting_values[item]=group==10&&part>=3?part-3:part;
    else if(group==9){
        memcpy(p->config_voice_snapshot,p->setting_values+KSET_CHARACTER_VOICE,sizeof(p->config_voice_snapshot));config_switch(p,b,3);
    }else if(group==12)config_close(p,b,0);
    else if(group==13){
        if(part==0)config_close(p,b,1);
        else if(part==2)for(unsigned i=0;i<KSET_COUNT;i++)p->setting_values[i]=bootstrap_setting_default(i);
    }else if(group==14&&part<3)config_switch(p,b,(unsigned)part);
    else if(group==15){
        if(part>=3&&part<=5){p->config_next_kind=part==3?5:part==4?18:6;p->status[0]=0;config_motion_begin(p,b,4);}
    }else if(group==200){for(unsigned i=0;i<33;i++)p->setting_values[KSET_CHARACTER_VOICE+i]=1;}
    else if(group==201)config_switch(p,b,1);
}
static void config_audition(MessagePanel *p,KBootstrap *b,int id){
    int group=(id>>8)-4;
    if(group<0||group>3)return;
    if(kconfig_audio_begin(&p->config_audio,b,(unsigned)group))
        snprintf(p->status,sizeof(p->status),"試聴音声を読み込めませんでした");
}
static void config_drag(MessagePanel *p,KBootstrap *b,int id,int x){
    if(p->config_motion)return;
    if(id<0||!config_slider(id)||!config_enabled(p,b,id))return;
    int item=config_item(id),start=(id>>8)==0?389:(id>>8)==1?495:408;
    int value=x-start,limit=bootstrap_setting_limit((unsigned)item);
    p->setting_values[item]=value<0?0:value>limit?limit:value;
    config_audition(p,b,id);
    p->config_audio.until=0;
}
/* Returns native slider ID+1 so touch drags retain the original control. */
static unsigned settings_panel_pointer(MessagePanel *p,KBootstrap *b,int x,int y,int activate){
    settings_panel_init(p,b);if(config_assets(p,b))return 0;
    if(p->config_motion)return 0;
    p->config_hover=-1;p->selected=~0u;
    for(unsigned i=0;i<p->config_area_count[p->setting_page];i++){
        const ConfigArea *a=&p->config_areas[p->setting_page][i];
        if(x<a->x0||x>=a->x1||y<a->y0||y>=a->y1)continue;
        p->selected=i;p->config_hover=a->id;
        if(!activate||!config_enabled(p,b,a->id))return 0;
        if(config_slider(a->id)){config_drag(p,b,a->id,x);return (unsigned)a->id+1;}
        config_activate(p,b,a->id);return 0;
    }
    return 0;
}
static void settings_panel_action(MessagePanel *p,KBootstrap *b,int action){
    settings_panel_init(p,b);
    if(p->config_motion)return;
    if(action==1){
        if(p->setting_page==3){memcpy(p->setting_values+KSET_CHARACTER_VOICE,p->config_voice_snapshot,sizeof(p->config_voice_snapshot));config_switch(p,b,1);}
        else config_close(p,b,0);
        return;
    }
    if(config_assets(p,b)){snprintf(p->status,sizeof(p->status),"設定画面の素材を読み込めませんでした");return;}
    if(action==7){config_activate(p,b,p->setting_page==3?200*256:13*256+2);return;}
    if(action==6||action==8||action==9){
        if(p->setting_page!=3)config_switch(p,b,(p->setting_page+(action==8?2:1))%3);
        return;
    }
    if(!action){config_activate(p,b,p->config_hover);return;}
    if(action<2||action>5)return;
    unsigned n=p->config_area_count[p->setting_page];
    if(p->selected>=n){p->selected=0;p->config_hover=p->config_areas[p->setting_page][0].id;return;}
    const ConfigArea *current=&p->config_areas[p->setting_page][p->selected];
    if((action==4||action==5)&&config_slider(current->id)&&config_enabled(p,b,current->id)){
        int item=config_item(current->id),value=p->setting_values[item]+(action==4?-1:1);
        if(value>=0&&value<=bootstrap_setting_limit((unsigned)item))p->setting_values[item]=value;
        config_audition(p,b,current->id);
        /* No pointer-up for key repeat: release after the last adjustment. */
        p->config_audio.until=SDL_GetTicks64()+250;
        return;
    }
    int cx=(current->x0+current->x1)/2,cy=(current->y0+current->y1)/2,best=-1;unsigned score=~0u;
    for(unsigned i=0;i<n;i++){
        const ConfigArea *a=&p->config_areas[p->setting_page][i];if(!config_enabled(p,b,a->id))continue;
        int dx=(a->x0+a->x1)/2-cx,dy=(a->y0+a->y1)/2-cy;
        int along=action==2?-dy:action==3?dy:action==4?-dx:dx;
        if(along<=0)continue;
        unsigned cross=(unsigned)abs(action<4?dx:dy),distance=(unsigned)along+cross*3;
        if(distance<score){score=distance;best=(int)i;}
    }
    if(best>=0){p->selected=(unsigned)best;p->config_hover=p->config_areas[p->setting_page][best].id;}
}
static int settings_panel_audio(MessagePanel *p,KHistoryAudio *saved,SDL_AudioDeviceID device,unsigned rate,unsigned channels){
    KConfigAudio *a=&p->config_audio;
    if(p->kind!=8)kconfig_audio_stop(a);
    if(a->until&&SDL_GetTicks64()>=a->until)kconfig_audio_release(a);
    if(!a->active){
        if(a->output_owned){a->output_owned=0;if(device&&khistory_audio_restore(saved,device))return -1;}
        if(p->kind!=8)kconfig_audio_clear(a);
        return 0;
    }
    if(!device||rate!=44100||channels!=2){
        kconfig_audio_stop(a);snprintf(p->status,sizeof(p->status),"試聴音声を出力できませんでした");return 0;
    }
    if(khistory_audio_suspend(saved,device))return -1;
    a->output_owned=1;
    if(p->config_audio_serial!=a->serial){SDL_PauseAudioDevice(device,1);SDL_ClearQueuedAudio(device);p->config_audio_serial=a->serial;}
    while(SDL_GetQueuedAudioSize(device)<4096){
        uint8_t pcm[2048];size_t size=kconfig_audio_read(a,p->setting_values,pcm,sizeof(pcm));
        if(!size)break;
        if(SDL_QueueAudio(device,pcm,(Uint32)size))return -1;
    }
    SDL_PauseAudioDevice(device,0);return 0;
}
#include "settings_draw.inc"

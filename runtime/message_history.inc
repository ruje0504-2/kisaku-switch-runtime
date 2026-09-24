static void message_history_record(KBootstrap *b){
    if(b->history_restore)return;
    const char *text=b->message_pending_size?b->message_pending:b->vm->text;
    size_t size=b->message_pending_size?b->message_pending_size:b->vm->text_size;
    if(!text||!size||size>4096)return;
    unsigned slot=b->history_next;memcpy(b->history[slot],text,size);b->history[slot][size]=0;
    strcpy(b->history_voice[slot],b->message_voice_name);b->message_voice_name[0]=0;
    b->history_next=(slot+1)%64;if(b->history_count<64)b->history_count++;
}
const char *bootstrap_history_voice(const KBootstrap *b,unsigned back){
    if(!b||back>=b->history_count)return NULL;
    const char *name=b->history_voice[(b->history_next+63-back)%64];return name[0]?name:NULL;
}
int bootstrap_history_draw(KBootstrap *b,unsigned back,KImage *out){
    KFont *font= b?bootstrap_ui_font(b):NULL;
    if(!b||back>=b->history_count||!font)return -1;
    if(!out->pixels){*out=(KImage){0,0,640,480,2560,calloc(640*480,4)};if(!out->pixels)return -1;}
    for(size_t i=0;i<640*480;i++){out->pixels[i*4]=28;out->pixels[i*4+1]=20;out->pixels[i*4+2]=16;out->pixels[i*4+3]=255;}
    /* Raw script bytes: decode with the runtime's effective encoding (auto
       detected GBK included), not with the configured encoding alone. */
    const char *text=b->history[(b->history_next+63-back)%64];KTextChar chars[4096];size_t count=0;
    if(bootstrap_decode_ui_text(b,text,strlen(text),chars,4096,&count))return -1;
    int x=32,y=64;
    for(size_t i=0;i<count;i++){
        if(chars[i].codepoint=='\r')continue;
        if(chars[i].codepoint=='\n'){x=32;y+=24;continue;}
        int width=(int)chars[i].columns*8;if(x+width>608){x=32;y+=24;}
        if(y>408)break;
        if(kfont_draw(font,out,chars[i].codepoint,x,y,16,16,0xffffff))return -1;
        x+=width;
    }
    return 0;
}
static const struct {const char *label,*group,*key;int initial,limit;} message_settings[]={
    {"settings.text_speed","Msg","ShowSpeed",86,124},
    {"settings.auto_speed","Msg","AutoMesSpeed",52,104},
    {"settings.voice_volume","Voice","Volume",83,104},
    {"settings.music_volume","Music","Volume",72,104},
    {"settings.effect_volume","Effect","Volume",83,104},
    {"settings.voice_playback","Voice","IsVoice",1,1},
    {"settings.music_playback","Music","IsMusic",1,1},
    {"settings.sound_effects","Effect","IsEffect",1,1},
    {"settings.visual_effects","Display","EffectSpeed",0,2},
    {"settings.message_transparency","Msg","Alpha",112,224},
    {"settings.mouth_animation","CONFIG","MAM",1,1},
    {"settings.message_red","Msg","Red",0,224},
    {"settings.message_green","Msg","Green",0,224},
    {"settings.message_blue","Msg","Blue",0,224},
    {"settings.male_voices","Voice","IsManVoice",1,1},
    {"settings.female_voices","Voice","IsWomanVoice",1,1},
    {"settings.read_skip","Msg","IsOneMes",0,1},
    {"settings.auto_dialogue","Msg","IsAutoMes",0,1},
    {"settings.movies_3d","CONFIG","Movie3D",1,1},
    {"settings.scene_sound_effects","Effect","IsHEffect",1,1},
    {"settings.right_click_menu","CONFIG","System",0,2},
    {"settings.scene_animation","CONFIG","Movie",1,1},
    {"settings.navigation_preview","CONFIG","SceneThum",0,1},
    {"settings.scene_effect_volume","Effect","HVolume",83,104},
    {"settings.movie_sync","Direct3D","MovieVSync",0,1},
    {"settings.sungeki_animation","CONFIG","IsSungekiAnime",1,1},
    {"settings.schedule_check","CONFIG","IsScheDuleCheck",1,1},
    {"settings.minigame_difficulty","Difficult","AllMiniGame",1,2},
    {"settings.sungeki_sound","CONFIG","IsSungekiSE",1,1},
    {"settings.text_speed_enabled","Msg","IsShowSpeed",1,1},
    {"settings.configuration_page","CONFIG","Page",0,3},
#define CHARACTER_SETTING(n) {"settings.character_voice_" #n,"Voice","IsCharVoice" #n,1,1}
    CHARACTER_SETTING(00),CHARACTER_SETTING(01),CHARACTER_SETTING(02),CHARACTER_SETTING(03),
    CHARACTER_SETTING(04),CHARACTER_SETTING(05),CHARACTER_SETTING(06),CHARACTER_SETTING(07),
    CHARACTER_SETTING(08),CHARACTER_SETTING(09),CHARACTER_SETTING(10),CHARACTER_SETTING(11),
    CHARACTER_SETTING(12),CHARACTER_SETTING(13),CHARACTER_SETTING(14),CHARACTER_SETTING(15),
    CHARACTER_SETTING(16),CHARACTER_SETTING(17),CHARACTER_SETTING(18),CHARACTER_SETTING(19),
    CHARACTER_SETTING(20),CHARACTER_SETTING(21),CHARACTER_SETTING(22),CHARACTER_SETTING(23),
    CHARACTER_SETTING(24),CHARACTER_SETTING(25),CHARACTER_SETTING(26),CHARACTER_SETTING(27),
    CHARACTER_SETTING(28),CHARACTER_SETTING(29),CHARACTER_SETTING(30),CHARACTER_SETTING(31),CHARACTER_SETTING(32)
#undef CHARACTER_SETTING
};
_Static_assert(sizeof(message_settings)/sizeof(*message_settings)==KSET_COUNT,"configuration capacity");
unsigned bootstrap_setting_count(void){return sizeof(message_settings)/sizeof(*message_settings);}
const char *bootstrap_setting_label(unsigned item){return item<bootstrap_setting_count()?message_settings[item].label:NULL;}
int bootstrap_setting_default(unsigned item){return item<bootstrap_setting_count()?message_settings[item].initial:-1;}
int bootstrap_setting_limit(unsigned item){return item<bootstrap_setting_count()?message_settings[item].limit:-1;}
int bootstrap_message_setting(KBootstrap *b,unsigned item,int change){
    if(!b||item>=bootstrap_setting_count())return -1;
    int value=option(b,message_settings[item].group,message_settings[item].key,message_settings[item].initial),limit=message_settings[item].limit;
    int64_t next=(int64_t)value+change;if(next<0)next=0;if(next>limit)next=limit;
    value=(int)next;
    if(change&&value&&item>=16&&item<=17)setting_put(b,"Msg",item==16?"IsAutoMes":"IsOneMes","0");
    if(change){char text[16];snprintf(text,sizeof(text),"%d",value);if(setting_put(b,message_settings[item].group,message_settings[item].key,text)||settings_save(b))return -1;
        if(item==9||(item>=11&&item<=13))message_skin_color(b,&b->message_skin.background);
        audio_settings_changed(b);
    }
    return value;
}

int bootstrap_settings_apply(KBootstrap *b,const int *values,unsigned count){
    if(!b||!values||count!=bootstrap_setting_count())return -1;
    for(unsigned i=0;i<count;i++)if(values[i]<0||values[i]>message_settings[i].limit)return -1;
    KSetting *previous=malloc(sizeof(b->settings));if(!previous)return -1;
    memcpy(previous,b->settings,sizeof(b->settings));unsigned previous_count=b->setting_count;char prior_error[sizeof(b->error)];memcpy(prior_error,b->error,sizeof(prior_error));int bad=0;
    for(unsigned i=0;i<count;i++){
        /* These retained API indices belong to the reference game's panels.
           Kisaku's CConfig must not create or rewrite their configuration. */
        if(i==10||i==14||i==15||i==18||i==20||i==22)continue;
        char text[16];snprintf(text,sizeof(text),"%d",values[i]);if(setting_put(b,message_settings[i].group,message_settings[i].key,text)){bad=1;break;}
    }
    if(!bad)bad=settings_save(b);
    if(bad){memcpy(b->settings,previous,sizeof(b->settings));b->setting_count=previous_count;memcpy(b->error,prior_error,sizeof(prior_error));}
    else {message_skin_color(b,&b->message_skin.background);audio_settings_changed(b);}
    free(previous);return bad?-1:0;
}

/* Validated save metadata stays owned by b->control_files during reconstruction. */
static void history_restore_apply(KBootstrap *b){
    const KControlRecord *record=b->history_restore;
    if(!record||!b->message_active||b->restore_pending)return;
    unsigned first=record->values[0].number==2?4:1;
    unsigned count=first==4?(unsigned)record->values[1].number:(record->count-1)/2;
    memset(b->history,0,sizeof(b->history));memset(b->history_voice,0,sizeof(b->history_voice));
    for(unsigned i=0;i<count;i++){
        strcpy(b->history[i],record->values[first+i*2].string);
        strcpy(b->history_voice[i],record->values[first+1+i*2].string);
    }
    if(b->backlog_restore){
        KBacklogSnapshot *s=b->backlog_restore;
        for(unsigned i=0;i<b->message_count;i++){free(b->messages[i].data);free(b->messages[i].text);}
        free(b->messages);b->messages=s->records;b->message_count=s->count;b->message_index=s->index;
        b->vm->globals[0][50].number=(b->vm->globals[0][50].number&~0x100)|(s->recording?0x100:0);
        s->records=NULL;s->count=0;kbacklog_snapshot_free(s);b->backlog_restore=NULL;
    }
    b->history_count=count;b->history_next=count%64;b->history_restore=NULL;b->message_voice_name[0]=0;
}

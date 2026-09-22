/* Saved CGirlStatus: 4ac3b0, 4b2110, 4b0f50, 4b2820.
   Flags belong to the selected save, never to the running VM. */
static const unsigned character_counts[8]={8,9,8,7,4,7,8,5};
static const unsigned character_slots[8]={8,9,6,6,4,7,7,5};
static const unsigned character_flags[8][9]={
    {202,210,224,413,237,237,239,402},
    {130,415,506,594,217,477,403,587,404},
    {207,419,500,602,501,502,649,638},
    {482,188,421,475,470,215,476},
    {199,228,245,417},
    {423,511,643,654,657,601,639},
    {492,509,522,214,395,531,532,405},
    {525,592,620,603,637}
};
static unsigned character_enabled(const KFlags *f,unsigned role,unsigned item){
    if(!f||role>=8||item>=character_counts[role]||f->byte_count<=1160)return 0;
    return f->bytes[character_flags[role][item]]==1||
        (role==1&&item==8&&f->bytes[635]==1);
}
static unsigned character_source_y(unsigned role,unsigned item){
    return 36+28*(item+(role==3&&item>=4));
}
/* 4ac3b0 also composes the small current-state strip below the status
   rows.  The strip is part of statfr.akb (the h_*.akb files are only loaded
   by the other CGirlStatus mode); leaving it out made every completed save
   look as if it had no current route state.  Keep the branch order from the
   native code: these flags are byte values, so the ==1 tests are deliberate
   and must not be widened to "non-zero". */
static unsigned character_byte(const KFlags *f,unsigned offset){
    return f&&offset<f->byte_count?f->bytes[offset]:0;
}
static unsigned character_dynamic_source_y(const KFlags *f,unsigned role){
    static const unsigned primary[8]={147,148,149,150,151,152,153,154};
    static const unsigned progress[8]={120,121,122,123,124,125,126,127};
    static const unsigned special[8]={171,172,173,174,175,176,177,178};
    static const unsigned final_state[8]={179,180,181,182,183,184,185,186};
    if(!f||role>=8)return 560;
    unsigned p=character_byte(f,primary[role]),q=character_byte(f,progress[role]);
    unsigned s=character_byte(f,special[role]),z=character_byte(f,final_state[role]);
    if(role==1){
        unsigned extra=character_byte(f,632);
        if(p==1&&q==0&&z==0)return 560;
        if(q==0||s!=0||z!=0){
            if((s==1||extra==1)&&z==0)return 656;
            if(z==1)return 592;
            return 560;
        }
        return 592;
    }
    if(role==6){
        unsigned extra=character_byte(f,618);
        if(p==1&&q==0&&z==0&&extra==0)return 560;
        if(q==0||s!=0||z!=0||extra!=0){
            if(s==1&&z==0&&extra==0)return 624;
            if(z==1&&extra==0)return 624;
            if(extra==1)return 592;
            return 560;
        }
        return 592;
    }
    /* The native CGirlStatus branches do not share one final-state row:
       Momo/Nadeshiko use the lower 0x2b0 strip, while Aoi uses 0x290.
       Collapsing every z==1 state to the common 0x250 row made those three
       completed characters show the wrong (ordinary) status artwork. */
    if(role==3||role==4){
        if(p==1&&q==0&&z==0)return 560;
        if(q==0||s!=0||z!=0){
            if(s==1&&z==0)return 624;
            if(z==1)return 688;
            return 560;
        }
        return 592;
    }
    if(role==7){
        if(p==1&&q==0&&z==0)return 560;
        if(q==0||s!=0||z!=0){
            if(s==1&&z==0)return 624;
            if(z==1)return 656;
            return 560;
        }
        return 592;
    }
    /* Aya and Shou keep the native "opening" strip even when their final
       completion byte is already set; their 004ac3b0 branch tests only the
       primary and progress bytes. */
    if(p==1&&q==0&&(role==2||role==5||z==0))return 560;
    if(q==0||s!=0||z!=0){
        if(s==1&&z==0)return 624;
        if(z==1)return 592;
        return 560;
    }
    return 592;
}
static void character_detail_motion(SaveMenu *m,KBootstrap *b,unsigned phase){
    unsigned base=phase<=2?16:24;
    int speed=bootstrap_message_setting(b,8,0);
    m->detail_phase=phase;m->detail_step=0;
    m->detail_steps=(b->vm->globals[0][50].number&0x4000)?base:speed==2?0:speed==1?base/4:base/2;
    m->detail_started=SDL_GetTicks64();
}
static void character_detail_frame(SaveMenu *m){
    if(!m->detail_phase)return;
    uint64_t step=(SDL_GetTicks64()-m->detail_started)/15;
    const Uint8 *keys=SDL_GetKeyboardState(NULL);
    if(keys[SDL_SCANCODE_LSHIFT]||keys[SDL_SCANCODE_RSHIFT]||keys[SDL_SCANCODE_LCTRL]||keys[SDL_SCANCODE_RCTRL])step=m->detail_steps;
    m->detail_step=step<m->detail_steps?(unsigned)step:m->detail_steps;
    if(m->detail_step<m->detail_steps)return;
    if(m->detail_phase==2){
        m->character_detail=0;rmt_free(&m->detail_artwork);rmt_free(&m->detail_status_artwork);
        rmt_free(&m->detail_image);
    }else if(m->detail_phase==4){m->detail_view=0;rmt_free(&m->detail_image);}
    m->detail_phase=0;
}
static void character_live_close(SaveMenu *m,KBootstrap *b){
    if(m->live_character&&!m->character_detail){
        m->live_character=m->active=0;b->file_modal=0;b->vm->globals[0][18]=(KValue){0,NULL};
        rmt_free(&m->portrait);m->draw_key=0;
    }
}
static void character_detail_action(SaveMenu *m,KBootstrap *b,int action){
    character_detail_frame(m);character_live_close(m,b);if(!m->character_detail||m->detail_phase)return;
    if(m->detail_view){if(action==0||action==1)character_detail_motion(m,b,4);return;}
    if(action==1){character_detail_motion(m,b,2);return;}
    const KFlags *f=m->states[(m->slot-1)%10];unsigned role=m->detail_index;
    if(action==2||action==3){
        int next=m->detail_hover;
        if(next<0){next=0;while(next<(int)character_counts[role]&&!character_enabled(f,role,(unsigned)next))next++;}
        else if(action==2){
            if(next==1000)next=(int)character_counts[role];
            int candidate=next-1;
            while(candidate>=0&&!character_enabled(f,role,(unsigned)candidate))candidate--;
            if(candidate>=0)next=candidate;
        }else if(next!=1000){
            next++;while(next<(int)character_counts[role]&&!character_enabled(f,role,(unsigned)next))next++;
        }
        m->detail_hover=next>=(int)character_counts[role]?1000:next;return;
    }
    if(m->live_character&&(action==4||action==5)){
        m->portrait_frame=(m->portrait_frame+(action==4?31:1))%32;m->portrait_started=SDL_GetTicks64();return;
    }
    if(action)return;
    if(m->detail_hover==1000){character_detail_motion(m,b,2);return;}
    if(m->detail_hover<0||!character_enabled(f,role,(unsigned)m->detail_hover))return;
    const char *name=bootstrap_character_image(role,(unsigned)m->detail_hover);
    rmt_free(&m->detail_image);
    if(!name||ui_asset(b,name,&m->detail_image)){
        snprintf(m->status,sizeof(m->status),"詳細画像を読み込めませんでした");return;
    }
    m->detail_view=1;character_detail_motion(m,b,3);
}
static void character_detail_pointer(SaveMenu *m,KBootstrap *b,int x,int y,int click){
    character_detail_frame(m);character_live_close(m,b);if(!m->character_detail||m->detail_phase)return;
    if(click==2||m->detail_view){if(click)character_detail_action(m,b,1);return;}
    if(m->live_character&&click==1&&y>=376&&y<416){
        if(x>=36&&x<132){character_detail_action(m,b,5);return;}
        if(x>=180&&x<276){character_detail_action(m,b,4);return;}
    }
    if(m->live_character)x-=320;
    m->detail_hover=-1;
    const KFlags *f=m->states[(m->slot-1)%10];unsigned row=0,role=m->detail_index;
    for(unsigned item=0;item<character_counts[role];item++)if(character_enabled(f,role,item)){
        if(x>=32&&x<303&&y>=64+(int)row*32&&y<91+(int)row*32)m->detail_hover=(int)item;
        row++;
    }
    if(x>=204&&x<304&&y>=400&&y<440)m->detail_hover=1000;
    if(click==1)character_detail_action(m,b,0);
}
static void character_detail_draw(SaveMenu *m,KBootstrap *b){
    static const char *names[]={"stathiro.akb","statmado.akb","statayan.akb","statmomo.akb","statnade.akb","statshou.akb","statyuri.akb","stataoi.akb"};
    unsigned role=m->detail_index,row=0;const KFlags *f=m->states[(m->slot-1)%10];
    if(role>=8||!f||ui_asset(b,"statfr.akb",&m->detail_artwork)||
       ui_asset(b,names[role],&m->detail_status_artwork)){
        snprintf(m->status,sizeof(m->status),"人物詳細データを読み込めませんでした");return;
    }
    KImage *d=&m->canvas,*a=&m->detail_artwork,*status=&m->detail_status_artwork;
    /* 4b1ec0/4b1c70 move the native 640px private surfaces -640 <-> -320. */
    int shift=-320;
    if(m->detail_phase==1)shift=-640+(int)(320*m->detail_step/m->detail_steps);
    if(m->detail_phase==2)shift=-320-(int)(320*m->detail_step/m->detail_steps);
    if(m->live_character)shift=0;
    ui_blit(d,a,0,0,640,480,shift,0,1);
    if(m->live_character&&m->portrait.pixels)ui_blit(d,&m->portrait,(m->portrait_frame%7)*240,(m->portrait_frame/7)*360,240,360,36,16,1);
    ui_blit(d,status,0,0,192,36,352+shift,24,1);
    for(unsigned item=0;item<character_counts[role];item++)if(character_enabled(f,role,item)){
        /* Hover uses the second vertical atlas page (537578). */
        static const unsigned page_height[8]={224,252,224,224,112,196,224,140};
        unsigned sy=character_source_y(role,item);
        if(m->detail_hover==(int)item)sy+=page_height[role];
        ui_blit(d,status,0,sy,272,28,352+shift,64+row*32,1);row++;
    }
    for(;row<character_slots[role];row++)ui_blit(d,a,540,480,16,28,352+shift,68+row*32,1);
    /* Current route-state strip at (48,424), from 4ac3b0's 216x32
       statfr source rows 0x230/0x250/0x270/0x290. */
    unsigned status_x=216;
    if(m->live_character){
        unsigned primary=character_byte(f,147+role),progress=character_byte(f,120+role);
        unsigned special=character_byte(f,171+role),final=character_byte(f,179+role),extra=role==6?character_byte(f,618):0;
        if(!(primary==1&&!progress&&(role==2||role==5||!final)&&!extra)&&
           (progress==0||special||final||extra)&&
           !((special==1||(role==1&&character_byte(f,632)==1))&&!final&&!extra))status_x=0;
    }
    ui_blit(d,a,status_x,character_dynamic_source_y(f,role),216,32,48+shift,424,1);
    unsigned number=f->bytes[120+role];
    if(number/10)ui_blit(d,a,(number/10)*24,480,24,40,436+shift,400,1);
    ui_blit(d,a,(number%10)*24,480,24,40,460+shift,400,1);
    int complete=1;for(unsigned i=0;i<12;i++)if(!f->bytes[1064+role*12+i])complete=0;
    if(complete)ui_blit(d,a,432,560,40,40,580+shift,20,1);
    ui_blit(d,a,m->detail_hover==1000?340:240,480,100,40,524+shift,400,1);
    if(m->detail_view&&m->detail_image.pixels){
        unsigned alpha=255;
        if(m->detail_phase==3)alpha=255*m->detail_step/m->detail_steps;
        if(m->detail_phase==4)alpha=255-255*m->detail_step/m->detail_steps;
        KImage *im=&m->detail_image;
        for(unsigned y=0;y<im->height&&y<480;y++)for(unsigned x=0;x<im->width&&x<640;x++){
            const uint8_t *s=im->pixels+y*im->stride+x*4;uint8_t *p=d->pixels+y*d->stride+x*4;
            unsigned v=s[3]*alpha/255;
            for(unsigned c=0;c<3;c++)p[c]=(uint8_t)(s[c]*v/255+p[c]*(255-v)/255);
        }
    }
}

/* 4ac3b0 live-only 32-view portrait selection. The saved-slot view never
   loads these atlases. Missing native branches deliberately leave it empty. */
static const char *character_portrait(const KFlags *f,unsigned role){
    static const char *names[8][6]={
        {"h_ou.akb","h_ho.akb","h_ka.akb","h_hu.akb","h_ni.akb",NULL},
        {"i_ni.akb","i_bo.akb",NULL,"i_hu.akb","i_ni.akb",NULL},
        {"b_ou.akb","b_ho.akb",NULL,"b_hu.akb","b_ni.akb",NULL},
        {"j_ou.akb","j_ho.akb","j_ka.akb","j_hu.akb","j_ni.akb",NULL},
        {"r_ou.akb","r_ho.akb","r_ka.akb","r_hu.akb","r_ni.akb",NULL},
        {"c_ou.akb","c_ho.akb",NULL,"c_hu.akb","c_ni.akb",NULL},
        {"e_ou.akb","e_ho.akb","e_ji.akb","e_hu.akb","e_ni.akb","e_ka.akb"},
        {"m_ou.akb","m_ho.akb","m_ji.akb","m_hu.akb","m_ni.akb",NULL}
    };
    if(role>=8)return NULL;
    unsigned p=character_byte(f,147+role),q=character_byte(f,120+role);
    unsigned z=character_byte(f,179+role),special=character_byte(f,171+role);
    unsigned extra=role==6?character_byte(f,618):0;
    if(p==1&&q==0&&(role==2||role==5||z==0)&&!extra)return names[role][0];
    if(q&&!special&&!z&&!extra)return names[role][4];
    if((special==1||(role==1&&character_byte(f,632)==1))&&!z&&!extra)return names[role][1];
    if(z==1&&!extra)return names[role][2];
    if(extra==1)return names[role][5];
    return names[role][3];
}
static int character_live_open(SaveMenu *m,KBootstrap *b,unsigned role){
    if(m->active||role>=8)return -1;
    KFlags *f=calloc(1,sizeof(*f));if(!f)return -1;
    f->byte_count=b->vm->byte_count;f->bytes=malloc(f->byte_count);
    if(!f->bytes){kflags_free(f);return -1;}memcpy(f->bytes,b->vm->bytes,f->byte_count);
    const char *name=character_portrait(f,role);KImage portrait={0};
    if(name&&(ui_asset(b,name,&portrait)||portrait.width<1680||portrait.height<1800)){rmt_free(&portrait);kflags_free(f);return -1;}
    kflags_free(m->states[0]);m->states[0]=f;
    m->slot=1;m->active=m->live_character=m->character_detail=1;
    m->detail_index=role;m->detail_hover=-1;m->detail_view=m->param_detail=m->overwrite=m->confirm_load=0;
    m->portrait_frame=0;m->portrait_started=SDL_GetTicks64();m->draw_key=0;m->status[0]=0;
    rmt_free(&m->portrait);m->portrait=portrait;
    rmt_free(&m->detail_artwork);rmt_free(&m->detail_status_artwork);rmt_free(&m->detail_image);
    character_detail_motion(m,b,1);return 0;
}

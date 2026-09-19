/* Original TEXT / SOUND / DISPLAY atlas layout and hit coordinates. */
typedef struct {unsigned item,page;int x,y,w,h;} SettingsControl;
static const SettingsControl settings_controls[]={
    {11,0,490,66,108,16},{12,0,490,90,108,16},{13,0,490,114,108,16},{9,0,490,138,108,16},
    {0,0,490,238,108,16},{16,0,446,314,174,22},{1,0,490,366,108,16},{17,0,446,386,174,22},
    {3,1,490,62,108,16},{6,1,446,82,174,22},{2,1,490,134,108,16},{5,1,446,154,174,22},
    {14,1,446,206,174,22},{15,1,446,258,174,22},{4,1,490,310,108,16},{7,1,446,330,174,22},{19,1,446,382,174,22},
    {8,2,382,62,238,22},{21,2,446,124,174,22},{18,2,446,238,174,22},{10,2,446,290,174,22},{22,2,446,342,174,22},{20,2,382,394,238,22}
};
static void settings_panel_init(MessagePanel *p,KBootstrap *b){
    if(p->setting_ready)return;
    for(unsigned i=0;i<bootstrap_setting_count();i++)p->setting_values[i]=bootstrap_message_setting(b,i,0);
    p->setting_ready=1;p->setting_page=0;p->selected=0;p->status[0]=0;
}
static void settings_panel_action(MessagePanel *p,KBootstrap *b,int action){
    settings_panel_init(p,b);
    if(action==1){p->setting_ready=0;p->kind=0;p->dismiss_menus=1;return;}
    if(action==0){
        int animation=bootstrap_prepare_animation_switch(b,(unsigned)p->setting_values[21]);
        if(animation<0){snprintf(p->status,sizeof(p->status),"表示切替の準備ができませんでした");return;}
        if(bootstrap_settings_apply(b,p->setting_values,bootstrap_setting_count())){snprintf(p->status,sizeof(p->status),"設定を保存できませんでした");return;}
        if(animation)p->animation_request=1;
        else if(b->message_active)b->vm->bytes[8190]=(uint8_t)p->setting_values[21];
        p->setting_ready=0;p->kind=0;p->dismiss_menus=1;return;
    }
    if(action==7){for(unsigned i=0;i<bootstrap_setting_count();i++)p->setting_values[i]=bootstrap_setting_default(i);return;}
    if(action==6||action==8||action==9){p->setting_page=(p->setting_page+(action==8?2:1))%3;for(unsigned i=0;i<sizeof(settings_controls)/sizeof(*settings_controls);i++)if(settings_controls[i].page==p->setting_page){p->selected=i;break;}return;}
    if(action==2||action==3){unsigned n=sizeof(settings_controls)/sizeof(*settings_controls);do{p->selected=(p->selected+(action==2?n-1:1))%n;}while(settings_controls[p->selected].page!=p->setting_page);}
    if(action==4||action==5){unsigned item=settings_controls[p->selected].item;int limit=bootstrap_setting_limit(item),value=p->setting_values[item]+(action==4?-1:1)*(limit>2?16:1);
        p->setting_values[item]=value<0?0:value>limit?limit:value;
        if(item>=16&&item<=17&&p->setting_values[item])p->setting_values[item==16?17:16]=0;
    }
}
static void settings_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    settings_panel_init(p,b);
    if(ui_canvas(&p->image)||ui_asset(b,"option1_p.rmt",&p->settings_artwork)||ui_asset(b,"option2_p.rmt",&p->sidebar_artwork))return;
    uint64_t key=ui_hash(ui_hash(1469598103934665603ULL,p->setting_values,sizeof(p->setting_values)),p->status,strlen(p->status));key^=(uint64_t)p->selected<<32|p->setting_page;
    if(p->native_kind==8&&p->native_key==key&&p->texture){SDL_Rect dst={160,0,960,720};SDL_RenderCopy(r,p->texture,NULL,&dst);return;}p->native_kind=8;p->native_key=key;
    KImage *d=&p->image,*a=&p->settings_artwork;ui_fill(d,0,0,640,480,0x17120c);
    ui_blit(d,a,0,0,640,28,0,0,0);ui_blit(d,a,340+(int)p->setting_page*88,80,84,24,340+(int)p->setting_page*88,2,0);
    ui_blit(d,a,(int)p->setting_page*272,102,272,386,368,36,0);
    /* Native animation load-reduction toggle maps to the Switch decoder
       workers; synchronization remains fixed by the presentation clock. */
    if(p->setting_page==2){ui_fill(d,376,152,256,60,0x25231a);ui_text(b,d,"軽減処理：自動",390,162,230,44,0xc7c1a4,0);
}
    ui_blit(d,&p->sidebar_artwork,0,0,166,168,6,308,0);
    for(unsigned i=0;i<sizeof(settings_controls)/sizeof(*settings_controls);i++){
        const SettingsControl *c=&settings_controls[i];if(c->page!=p->setting_page)continue;
        int value=p->setting_values[c->item],limit=bootstrap_setting_limit(c->item);
        if(limit>2){int x=c->x+value*(c->w-1)/255;ui_blit(d,a,0,32,16,16,x-8,c->y,1);}
        else if(c->item==22){ui_blit(d,a,160+value*88,78,84,22,c->x+value*88,c->y,0);}
        else if(limit==1){ui_blit(d,a,value?640:728,44,86,22,c->x+(value?0:88),c->y,0);}
        else{ui_blit(d,a,(c->item==20?240:480)+value*80,534,80,22,c->x+value*80,c->y,0);}
        if(i==p->selected){ui_fill(d,c->x-4,c->y-3,c->w+8,2,0xffdf74);ui_fill(d,c->x-4,c->y+c->h+1,c->w+8,2,0xffdf74);}
    }
    if(p->setting_page==0)ui_fill(d,390,168,228,32,(unsigned)(p->setting_values[11]<<16|p->setting_values[12]<<8|p->setting_values[13]));
    ui_blit(d,a,552,564,248,22,376,444,0);
    ui_fill(d,8,32,344,260,0xa08948);ui_scale(d,&b->layers[0],12,36,336,252);
    ui_text(b,d,"上下：選択\n左右：調整\nL/R・Y：ページ\nA：決定\nB：キャンセル\nX：デフォルト",180,314,176,140,0xffffff,0);

    if(p->status[0])ui_text(b,d,p->status,24,252,320,40,0xffd060,0);
    ui_present(r,d,&p->texture);
}

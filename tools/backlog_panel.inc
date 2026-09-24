/* Kisaku CBackLog: 4da380, 4d8450, 4d91d0 and 4d9400.
   The newest record occupies row 5; scrolling advances one record, not a page. */
static void backlog_panel_init(MessagePanel *p){
    if(p->viewing)return;
    p->viewing=1;p->backlog_offset=0;p->backlog_hover=-1;p->backlog_mouse_drag=0;
    p->native_kind=0;p->status[0]=0;
}
static unsigned backlog_limit(const KBootstrap *b){return bootstrap_backlog_count(b)>6?bootstrap_backlog_count(b)-6:0;}
static int backlog_assets(MessagePanel *p,KBootstrap *b){
    if(ui_asset(b,"kisaku_DL_backlog_p.akb",&p->history_artwork)||p->history_artwork.width!=640||p->history_artwork.height!=480)return -1;
    if(p->backlog_area_count)return 0;
    uint8_t *data=NULL;size_t size=0;
    if(ai6_read_named(&b->data,"kisaku_DL_backlog.area",&data,&size)){free(data);return -1;}
    if(size!=184||data[0]!=9||data[1]||data[2]||data[3]){free(data);return -1;}
    for(unsigned i=0;i<9;i++){
        int v[5];for(unsigned j=0;j<5;j++){const uint8_t *q=data+4+i*20+j*4;v[j]=(int32_t)(q[0]|(uint32_t)q[1]<<8|(uint32_t)q[2]<<16|(uint32_t)q[3]<<24);}
        const int expected_id=i<6?(int)i:i<8?256+(int)i-6:512;
        if(v[0]!=expected_id||v[1]<0||v[2]<0||v[3]>640||v[4]>480||v[1]>=v[3]||v[2]>=v[4]){free(data);return -1;}
        p->backlog_areas[i]=(ConfigArea){v[0],v[1],v[2],v[3],v[4]};
    }
    free(data);p->backlog_area_count=9;return 0;
}
static int backlog_row_back(const MessagePanel *p,const KBootstrap *b,int row){
    if(row<0||row>5)return -1;
    unsigned back=p->backlog_offset+5-(unsigned)row;
    return back<bootstrap_backlog_count(b)?(int)back:-1;
}
static int backlog_hit(const MessagePanel *p,int x,int y){
    for(unsigned i=0;i<p->backlog_area_count;i++){
        const ConfigArea *a=&p->backlog_areas[i];
        if(x>=a->x0&&x<a->x1&&y>=a->y0&&y<a->y1)return a->id;
    }
    return -1;
}
static void backlog_close(MessagePanel *p,KBootstrap *b){
    if(b->letter_backlog)bootstrap_message_hide(b,0);
    history_voice_stop(p);p->kind=p->viewing=p->backlog_mouse_drag=0;p->dismiss_menus=1;b->message_hover=-1;
}
static void backlog_scroll(MessagePanel *p,KBootstrap *b,int delta,int exit_at_latest){
    backlog_panel_init(p);
    if(exit_at_latest&&delta<0&&!p->backlog_offset){backlog_close(p,b);return;}
    int next=(int)p->backlog_offset+delta,max=(int)backlog_limit(b);
    if(next<0)next=0;
    if(next>max)next=max;
    if((unsigned)next!=p->backlog_offset){history_voice_stop(p);p->backlog_offset=(unsigned)next;p->status[0]=0;}
}
static void backlog_drag(MessagePanel *p,KBootstrap *b,int y){
    int max=(int)backlog_limit(b);if(!max)return;
    /* 4d9b00: thumb centre 34..446, nearest integer, inverted history. */
    if(y<34)y=34;
    if(y>446)y=446;
    int next=max-((y-34)*max+206)/412;
    backlog_scroll(p,b,next-(int)p->backlog_offset,0);p->backlog_hover=512;
}
static void backlog_activate(MessagePanel *p,KBootstrap *b){
    if(p->voice_loading||p->voice_pcm){history_voice_stop(p);return;}
    int id=p->backlog_hover;
    if(id>=0&&id<6){
        int back=backlog_row_back(p,b,id);
        if(back>=0&&bootstrap_backlog_has_voice(b,(unsigned)back)){p->back=(unsigned)back;history_voice_play(p,b);}
    }else if(id==256)backlog_scroll(p,b,1,0);
    else if(id==257)backlog_scroll(p,b,-1,0);
}
static unsigned backlog_pointer(MessagePanel *p,KBootstrap *b,int x,int y,int click){
    backlog_panel_init(p);
    if(click==2){if(p->voice_loading||p->voice_pcm)history_voice_stop(p);else backlog_close(p,b);return 0;}
    if(backlog_assets(p,b))return 0;
    p->backlog_hover=backlog_hit(p,x,y);
    if(click==1){
        if(p->backlog_hover==512&&backlog_limit(b)){backlog_drag(p,b,y);return 1;}
        backlog_activate(p,b);
    }
    return 0;
}
static void backlog_panel_action(MessagePanel *p,KBootstrap *b,int action){
    backlog_panel_init(p);
    if(action==1){if(p->voice_loading||p->voice_pcm)history_voice_stop(p);else backlog_close(p,b);return;}
    if(action==8||action==9){backlog_scroll(p,b,action==8?6:-6,0);return;}
    if(!action){backlog_activate(p,b);return;}
    if(action<2||action>5)return;
    /* 4d8cb0's directional cursor transitions, without a platform pointer warp. */
    int id=p->backlog_hover,dir=action-2;unsigned max=backlog_limit(b);
    if(id<0)id=257;
    else if(id<6){
        if(dir==0&&id>0)id--;
        else if(dir==1&&id<5)id++;
        else if(dir==3)id=id==0?256:id==5||!max?257:512;
    }else if(id==256||id==257){
        const int table[2][4]={{256,512,0,256},{512,257,4,257}};
        id=table[id-256][dir];if(id==512&&!max)id=dir==0?256:257;
    }else if(id==512){
        if(!max)id=257;
        else if(dir==0)id=256;
        else if(dir==1)id=257;
        else if(dir==2){unsigned row=(p->backlog_offset*40/max+5)/10;if(row>4)row=4;id=4-(int)row;}
    }
    p->backlog_hover=id;
}
static int backlog_text(KBootstrap *b,KImage *d,const char *text,int y){
    KTextChar chars[4096];KTextPosition positions[4096];size_t count=0;
    KFont *font=ui_font(b);if(!font||bootstrap_decode_ui_text(b,text,strlen(text),chars,4096,&count))return -1;
    /* 4d8fe0: 16px glyphs, 18px lines, x=32, width=560, height=54.
       Keep the original kinsoku layout; the portable history currently stores
       flattened text, not the native record's executable drawing commands. */
    KImage row={0,0,608,54,d->stride,d->pixels+y*d->stride};int x=32,cy=0;
    for(size_t start=0;start<count;){
        size_t end=start;while(end<count&&chars[end].codepoint>=32)end++;
        if(end>start){
            if(ktext_layout_native(chars+start,end-start,32,0,592,54,8,18,&x,&cy,positions))return -1;
            for(size_t i=0;i<end-start;i++)if(kfont_draw(font,&row,chars[start+i].codepoint,positions[i].x,positions[i].y,16,16,0xffffff))return -1;
        }
        if(end<count){if(chars[end].codepoint=='\n'){x=32;cy+=18;}else if(chars[end].codepoint!='\r')return -1;end++;}
        start=end;
    }
    return 0;
}
static void backlog_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    backlog_panel_init(p);if(ui_canvas(&p->image))return;
    if(backlog_assets(p,b)){
        ui_scale(&p->image,&b->layers[0],0,0,640,480);
        ui_text_key(b,&p->image,"status.history_assets_with_back",120,210,400,60,0xffffff);
        ui_present(r,&p->image,&p->texture);return;
    }
    unsigned state[]={p->backlog_offset,(unsigned)p->backlog_hover,p->back,b->history_next,bootstrap_backlog_count(b),b->text_count,p->voice_loading,p->voice_pcm!=NULL,p->backlog_mouse_drag};
    uint64_t key=ui_hash(1469598103934665603ULL,state,sizeof(state));
    if(p->native_kind==5&&key==p->native_key&&p->texture){SDL_Rect dst={160,0,960,720};SDL_RenderCopy(r,p->texture,NULL,&dst);return;}
    KImage *d=&p->image,*art=&p->history_artwork;ui_scale(d,&b->layers[0],0,0,640,480);
    const uint8_t black[]={0,0,0,192};
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *bg=x>=616?art->pixels+y*art->stride+x*4:y%80>=76?art->pixels+(y%80-76)*art->stride+x*4:black;
        kdialog_pixel(d->pixels+y*d->stride+x*4,bg);
    }
    int playing=p->voice_loading||p->voice_pcm;
    for(int row=0;row<6;row++){
        int back=backlog_row_back(p,b,row);if(back<0)continue;
        int has_voice=bootstrap_backlog_has_voice(b,(unsigned)back);
        int selected=has_voice&&(playing?(unsigned)back==p->back:p->backlog_hover==row);
        if(selected)ui_blit(d,art,0,playing?84:4,616,80,0,row*80-1,1);
        int failed;
        if(bootstrap_backlog_native(b)){
            KImage target={0,0,640,54,d->stride,d->pixels+(row*80+10)*d->stride};
            KBacklogVoices voices={0};char why[256]={0};
            failed=bootstrap_backlog_replay(b,(unsigned)back,&target,&voices,why);
            kbacklog_voices_free(&voices);
        }else failed=backlog_text(b,d,b->history[(b->history_next+63-(unsigned)back)%64],row*80+10);
        if(failed){
            snprintf(p->status,sizeof(p->status),"%s",ui_string(b,"status.history_draw_failed"));
            ui_text(b,d,p->status,32,row*80+10,560,54,0xffffff,0);
        }
        if(has_voice)ui_blit(d,art,32,selected&&playing?184:164,16,20,4,row*80+10,1);
    }
    unsigned max=backlog_limit(b);
    for(int i=0;i<2;i++){
        int disabled=i?p->backlog_offset==0:p->backlog_offset>=max;
        int control_state=disabled?3:p->backlog_hover==256+i?1:0;
        ui_blit(d,art,i*16,164+control_state*20,16,20,620,i?456:4,1);
    }
    if(max)ui_blit(d,art,48,164+(p->backlog_mouse_drag?2:p->backlog_hover==512?1:0)*20,16,20,620,436-(int)(p->backlog_offset*412/max),1);
    p->native_kind=5;p->native_key=key;ui_present(r,d,&p->texture);
}

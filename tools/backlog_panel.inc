/* Native backlog.area has six 80-pixel rows and a 32-pixel scrollbar. */
static void backlog_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    if(ui_canvas(&p->image)||ui_asset(b,"baklog.rmt",&p->history_artwork))return;
    unsigned state[]={p->back,p->viewing,b->history_next,b->history_count,b->text_count,p->voice_loading,p->voice_pcm!=NULL,(unsigned)p->cg_y};
    uint64_t key=ui_hash(1469598103934665603ULL,state,sizeof(state));
    if(p->native_kind==5&&key==p->native_key&&p->texture){SDL_Rect dst={160,0,960,720};SDL_RenderCopy(r,p->texture,NULL,&dst);return;}
    p->native_kind=5;p->native_key=key;
    KImage *d=&p->image;ui_fill(d,0,0,640,480,0x18130d);
    unsigned first=p->back/6*6,count=b->history_count>first?b->history_count-first:0;if(count>6)count=6;
    if(!count)ui_text(b,d,"メッセージ履歴がありません",144,216,448,40,0xffffff,0);
    for(unsigned row=0;row<count;row++){
        unsigned back=first+count-1-row,index=(b->history_next+63-back)%64;int y=(int)row*80;
        if(back==p->back)ui_fill(d,0,y,608,79,0x3a3020);
        ui_text(b,d,b->history[index],36,y+8,560,64,back==p->back?0xffe394:0xffffff,1);
        if(bootstrap_history_voice(b,back))ui_blit(d,&p->history_artwork,back==p->back?64:32,48,32,32,1,y+22,2);
        ui_fill(d,0,y+79,608,1,0x5b4c32);
    }
    ui_blit(d,&p->history_artwork,0,0,32,480,608,0,2);
    if(b->history_count){int y=32+(int)((b->history_count-1-p->back)*384/(b->history_count>1?b->history_count-1:1));ui_blit(d,&p->history_artwork,64,80,32,32,608,y,2);}
    if(p->viewing&&b->history_count){
        ui_fill(d,20,20,576,440,0x241c11);
        int lines=ui_text_scroll(b,d,b->history[(b->history_next+63-p->back)%64],36,36,544,392,0xffffff,1,(unsigned)p->cg_y);p->cg_scroll_limit=lines>19?(unsigned)lines-19:0;
        ui_text(b,d,"上下: スクロール Y: 一覧 A: 音声",48,432,528,24,0xffe394,0);
    }
    ui_present(r,d,&p->texture);
}

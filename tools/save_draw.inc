/* 479470/477430/476920: native 100-slot rows and overlapping page tabs. */
static int save_menu_assets(SaveMenu *m,KBootstrap *b){
    if(ui_canvas(&m->canvas)||ui_asset(b,"kisaku_DL_save_p.akb",&m->artwork)||
       ui_asset(b,"kisaku_DL_load_p.akb",&m->load_artwork)||ui_asset(b,"kisaku_DL_text_p.akb",&m->common))return -1;
    if(m->area_count)return 0;
    uint8_t *data=NULL;size_t size=0;
    if(ai6_read_named(&b->data,"save_load_01.area",&data,&size)){free(data);return -1;}
    if(size!=624||data[0]!=31||data[1]||data[2]||data[3]){free(data);return -1;}
    for(unsigned i=0;i<31;i++)for(unsigned j=0;j<5;j++){
        const uint8_t *p=data+4+i*20+j*4;
        m->areas[i][j]=(int32_t)((uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24);
    }
    free(data);m->area_count=31;return 0;
}
static int save_menu_hit(const SaveMenu *m,int x,int y){
    for(unsigned i=0;i<m->area_count;i++){
        const int *a=m->areas[i];if(x>=a[1]&&y>=a[2]&&x<a[3]&&y<a[4])return a[0];
    }
    return -1;
}
static void save_menu_digits(KImage *d,const KImage *a,const char *text,int x,int y,int highlight){
    for(unsigned i=0;text[i];i++){
        int sx=text[i]==':'?208:text[i]=='/'?200:text[i]=='-'?216:text[i]>='0'&&text[i]<='9'?120+(text[i]-'0')*8:-1;
        if(sx>=0)ui_blit(d,a,sx,80+highlight*12,8,12,x+(int)i*8,y,1);
    }
}
/* 4747a0's eight independently ordered character state branches. */
static unsigned save_character_state(const uint8_t *v,unsigned i){
    int special=(i==2||i==5),extra=i==6?v[618]:0;
    if(v[147+i]==1&&!v[120+i]&&(special||(!v[179+i]&&!extra)))return 100;
    if(v[120+i]&&!v[171+i]&&(special||(!v[179+i]&&!extra)))return 120;
    if(v[171+i]==1&&!v[179+i]&&!extra)return i==1?180:140;
    if(v[179+i]==1&&!extra)return i==3||i==4?200:i==6?220:i==7?240:160;
    if(i==6&&extra==1)return 160;
    return 80;
}
static void save_menu_book(SaveMenu *m){
    KImage *d=&m->canvas,*a=&m->artwork;
    ui_blit(d,a,0,140,268,212,8,60,1);
    unsigned row=(unsigned)(m->slot-1)%10;const KFlags *f=m->states[row];
    if(!f||m->selector||m->focus)return;
    static const unsigned present[8]={108,109,111,112,113,114,115,116};
    for(unsigned i=0;i<8;i++)if(f->bytes[present[i]]){
        int x=8+(int)(i/4)*132,y=60+(int)(i%4)*52;
        ui_blit(d,a,448+(int)(i/4)*36,80+(int)(i%4)*36,36,36,x+16,y+8,1);
        ui_blit(d,a,280,80+(int)i*12,56,12,x+60,y+12,1);
        ui_blit(d,a,336,(int)save_character_state(f->bytes,i),64,20,x+56,y+24,1);
    }
}
static void save_menu_draw(SaveMenu *m,KBootstrap *b,SDL_Renderer *r){
    if(m->live_character){
        character_detail_frame(m);character_live_close(m,b);if(!m->active)return;
        if(ui_canvas(&m->canvas))return;
        uint64_t now=SDL_GetTicks64();
        if(!m->detail_phase&&!m->detail_view&&now-m->portrait_started>=40){
            unsigned steps=(unsigned)((now-m->portrait_started)/40);
            m->portrait_frame=(m->portrait_frame+32-steps%32)%32;m->portrait_started+=steps*40;
        }
        memcpy(m->canvas.pixels,b->layers[0].pixels,640*480*4);
        character_detail_draw(m,b);
        if(m->detail_phase==1||m->detail_phase==2){
            unsigned a=m->detail_steps?255*m->detail_step/m->detail_steps:255;
            if(m->detail_phase==2)a=255-a;
            for(unsigned i=0;i<640*480;i++)for(unsigned c=0;c<3;c++)
                m->canvas.pixels[i*4+c]=(uint8_t)(m->canvas.pixels[i*4+c]*a/255+b->layers[0].pixels[i*4+c]*(255-a)/255);
        }
        ui_present(r,&m->canvas,&m->texture);return;
    }

    if(save_menu_assets(m,b)){snprintf(m->status,sizeof(m->status),"画面データを読み込めませんでした");return;}
    if(m->character_detail)character_detail_frame(m);
    int state[]={m->save,m->slot,m->selector,m->overwrite,m->loading,m->confirm_load,m->button,m->focus,m->hover,m->last_slot,(int)m->param_detail,(int)m->character_detail,(int)m->detail_index,m->detail_hover,(int)m->detail_step,(int)m->detail_view,(int)m->detail_phase};
    uint64_t key=ui_hash(ui_hash(1469598103934665603ULL,state,sizeof(state)),m->status,strlen(m->status));key=ui_hash(key,m->note,strlen(m->note));
    unsigned param_step=0;if(m->param_detail){uint64_t elapsed=(SDL_GetTicks64()-m->param_started)/15;param_step=elapsed<m->param_steps?(unsigned)elapsed:m->param_steps;key=ui_hash(key,&param_step,sizeof(param_step));}
    if(key==m->draw_key&&m->texture){SDL_Rect dst={160,0,960,720};SDL_RenderCopy(r,m->texture,NULL,&dst);return;}m->draw_key=key;
    KImage *d=&m->canvas,*a=&m->artwork;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=d->pixels+y*d->stride+x*4;const uint8_t *s=b->layers[0].pixels+y*b->layers[0].stride+x*4;
        for(unsigned c=0;c<3;c++)p[c]=(uint8_t)(s[c]*63/255);
        p[3]=255;
    }
    ui_blit(d,m->save?a:&m->load_artwork,0,0,116,28,3,2,1);
    ui_blit(d,a,596,0,28,20,612,0,1);
    unsigned page=(unsigned)(m->slot-1)/10,row=(unsigned)(m->slot-1)%10;
    /* Low-numbered pages have higher native sprite priority at overlaps. */
    for(int i=9;i>=0;i--)ui_blit(d,a,116+i*48,(unsigned)i==page?40:m->hover==512+i?20:0,48,20,232+i*38,0,1);
    save_menu_book(m);
    if(!m->selector&&!m->focus&&m->states[row]){
        KImage *preview=&m->previews[row];
        if(!preview->pixels&&bootstrap_saved_param_image(b,m->states[row],preview))snprintf(m->status,sizeof(m->status),"パラメータを読み込めませんでした");
        if(preview->pixels){
            unsigned height=(unsigned)((float)(268.0/preview->width)*preview->height);
            ui_scale(d,preview,8,272,268,height);
            if(m->hover>>8==6){uint8_t highlight[4]={0,255,255,128};for(unsigned y=272;y<272+height;y++)for(unsigned x=8;x<276;x++)kdialog_pixel(d->pixels+y*d->stride+x*4,highlight);}
        }
    }
    for(unsigned i=0;i<10;i++){
        unsigned slot=page*10+i;int selected=i==row&&!m->focus,shift=selected?-20:0;
        int highlight=(m->valid[i]&&slot+1==(unsigned)m->last_slot);
        unsigned sy=m->selector?428:m->states[i]&&m->states[i]->bytes[1031]?356:284;
        ui_blit(d,a,280,(int)sy+selected*36,selected?364:344,36,296+shift,28+(int)i*36,1);
        ui_blit(d,a,(int)(slot/10)*12,92+highlight*16,12,16,320+shift,36+(int)i*36,1);
        ui_blit(d,a,(int)(slot%10)*12,92+highlight*16,12,16,332+shift,36+(int)i*36,1);
        if(!m->valid[i]){ui_blit(d,a,40,28,52,16,418+shift,36+(int)i*36,1);continue;}
        char date[40];time_t stamp=(time_t)m->dates[i];struct tm *tm=localtime(&stamp);
        if(tm){strftime(date,sizeof(date),"%Y-%m/%d-%H:%M",tm);save_menu_digits(d,a,date,380+shift,32+(int)i*36,highlight);}
        ui_fill(d,344+shift,44+(int)i*36,200,16,0);
        ui_text(b,d,m->comments[i],344+shift,44+(int)i*36,200,16,highlight?0xff8400:0xffffff,1);
        unsigned stage=m->info[i].stage,day=m->info[i].scene,month=stage>=2?2:1;
        if(stage>3)stage=0;
        ui_blit(d,a,400,m->selector?208+highlight*16:(3-(int)stage)*32+80+highlight*16,48,16,564+shift,46+(int)i*36,1);
        if(!m->selector){
            ui_blit(d,a,(int)month*12,92+highlight*16,12,16,548+shift,30+(int)i*36,1);
            ui_blit(d,a,72,60+highlight*16,28,16,560+shift,30+(int)i*36,1);
            if(day>=10&&day<100)ui_blit(d,a,(int)(day/10)*12,92+highlight*16,12,16,588+shift,30+(int)i*36,1);
            ui_blit(d,a,(int)(day%10)*12,92+highlight*16,12,16,600+shift,30+(int)i*36,1);
            ui_blit(d,a,56,60+highlight*16,16,16,612+shift,30+(int)i*36,1);
        }
    }
    ui_blit(d,&m->common,244,316+(m->focus==2?32:0),130,32,510,448,1);
    if(m->status[0]&&!m->overwrite&&!m->confirm_load&&!m->loading)ui_text(b,d,m->status,16,400,480,40,0xffdf78,0);
    if(m->overwrite||m->confirm_load){
        KImage *dialog=m->overwrite?&m->note_artwork:&m->dialog_artwork;
        if(ui_asset(b,m->overwrite?"dialog_p.akb":"kisaku_DL_dialog_p.akb",dialog))return;
        /* 4781b0 / 4bfaa0(5): native body at y=324; dialog2.area +48. */
        if(m->confirm_load)for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
            for(unsigned c=0;c<3;c++)d->pixels[y*d->stride+x*4+c]=(uint8_t)(d->pixels[y*d->stride+x*4+c]*63/255);
        ui_blit(d,dialog,0,0,428,88,106,324,1);
        if(m->overwrite){
            ui_blit(d,dialog,0,m->valid[row]?216:264,276,24,182,332,1);
            ui_fill(d,140,356,368,20,0);
            ui_text(b,d,m->note,140,356,368,20,0xffffff,0);
            if(m->status[0])ui_text(b,d,m->status,140,416,400,40,0xffdf78,0);
        }else{
            ui_blit(d,dialog,0,240,276,24,182,332,1);
            ui_blit(d,dialog,0,136,180,24,230,356,1);
        }
        ui_blit(d,dialog,276,88+(m->button==0?20:0),96,20,314,384,1);
        ui_blit(d,dialog,180,88+(m->button==1?20:0),96,20,230,384,1);
    }
    if(m->param_detail){
        const KImage *preview=&m->previews[row];
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)for(unsigned c=0;c<3;c++)d->pixels[y*d->stride+x*4+c]=(uint8_t)(d->pixels[y*d->stride+x*4+c]*63/255);
        unsigned alpha=m->param_steps?255*param_step/m->param_steps:255;
        int left=(640-(int)preview->width)/2;
        for(unsigned y=0;y<preview->height;y++)for(unsigned x=0;x<preview->width;x++){
            const uint8_t *s=preview->pixels+y*preview->stride+x*4;uint8_t *out=d->pixels+(272+y)*d->stride+(left+(int)x)*4;
            for(unsigned c=0;c<3;c++)out[c]=(uint8_t)(s[c]*alpha/255+out[c]*(255-alpha)/255);
        }
    }
    if(m->character_detail)character_detail_draw(m,b);
    if(m->loading){
        ui_fill(d,170,200,300,158,0xa08948);ui_fill(d,173,203,294,152,0x241a12);
        ui_text(b,d,"ロード中…",188,224,260,24,0xffffff,0);
        ui_text(b,d,"B: 戻る",198,312,260,40,0xffde7a,0);
    }
    ui_present(r,d,&m->texture);
}

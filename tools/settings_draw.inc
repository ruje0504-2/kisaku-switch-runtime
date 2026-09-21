/* Native CConfig atlas rectangles: 4c47c0 and 4c04d0..4c14a0. */
static void config_stamp(MessagePanel *p,unsigned atlas,int sx,int sy,int w,int h,int x,int y){
    x+=p->config_dx;y+=p->config_dy;
    if(p->config_alpha==255){ui_blit(&p->image,&p->config_art[atlas],sx,sy,w,h,x,y,1);return;}
    const KImage *art=&p->config_art[atlas];
    for(int row=0;row<h;row++)for(int col=0;col<w;col++){
        if(x+col<0||x+col>=640||y+row<0||y+row>=480||sx+col<0||sy+row<0||(unsigned)(sx+col)>=art->width||(unsigned)(sy+row)>=art->height)continue;
        const uint8_t *s=art->pixels+(sy+row)*art->stride+(sx+col)*4;uint8_t *d=p->image.pixels+(y+row)*p->image.stride+(x+col)*4;
        unsigned alpha=s[3]*p->config_alpha/255;
        for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(s[c]*alpha/255+d[c]*(255-alpha)/255);
    }
}
static int config_state(MessagePanel *p,KBootstrap *b,int id,int selected){
    if(!config_enabled(p,b,id))return 3;
    return selected?2:p->config_hover==id?1:0;
}
static void config_toggle(MessagePanel *p,KBootstrap *b,int group,int y,int item){
    for(int value=0;value<2;value++){
        int id=group*256+value,state=config_state(p,b,id,p->setting_values[item]==value);
        config_stamp(p,0,224,28+(1-value)*80+state*20,64,20,561-value*48,y);
    }
}
static void config_three(MessagePanel *p,KBootstrap *b,int group,int item,int sx,int x,int y){
    for(int value=2;value>=0;value--){
        int state=config_state(p,b,group*256+value,p->setting_values[item]==value);
        config_stamp(p,2,sx,value*80+state*20,52,20,x+value*40,y);
    }
}
static void config_line(MessagePanel *p,int y){config_stamp(p,0,188,0,324,4,316,y);}
static void config_audio_slider(MessagePanel *p,KBootstrap *b,int group,int line_y,int marker_y,int label_y){
    int id=group*256+2,on=config_enabled(p,b,id),item=config_item(id);
    config_stamp(p,1,112,on?36:40,108,4,408,line_y);
    config_stamp(p,1,112,24,12,12,407,label_y);config_stamp(p,1,124,24,12,12,504,label_y);
    if(on)config_stamp(p,1,136,24,4,12,408+p->setting_values[item],marker_y);
}
static void config_draw_page(MessagePanel *p,KBootstrap *b,unsigned page){
    if(page==0){
        const int titles[][3]={{60,288,76},{275,288,100},{331,288,124},{387,288,148}};
        for(unsigned i=0;i<4;i++){config_stamp(p,0,titles[i][1],titles[i][2],172,24,316,titles[i][0]);}
        const int lines[]={84,300,356,412};for(unsigned i=0;i<4;i++)config_line(p,lines[i]);
        config_stamp(p,0,244,444,256,28,344,232);
        config_stamp(p,0,116,12,12,12,384,96);config_stamp(p,0,128,12,20,12,607,96);
        int page_dx=p->config_dx;p->config_dx=p->config_preview_dx;
        config_stamp(p,0,288,12,294,32,346,182);p->config_dx=page_dx;
        for(int i=0;i<4;i++){
            if(i<3)config_stamp(p,0,116+i*12,0,12,12,364,110+i*19);
            else config_stamp(p,0,152,0,32,12,354,168);
            config_stamp(p,0,188,4,228,4,389,113+i*19);
            config_stamp(p,0,184,0,4,12,389+p->setting_values[config_item(i)],108+i*19);
        }
        /* Preview scaling is 255/224 (548a80); CMesWnd itself uses /184. */
        uint8_t color[4];const unsigned items[]={13,12,11,9};
        for(unsigned c=0;c<4;c++){int v=p->setting_values[items[c]];color[c]=(uint8_t)((c==3?224-v:v)*(255.0/224.0));}
        for(int y=186;y<208;y++)for(int x=350;x<635;x++){
            int dx=x+p->config_preview_dx;if(dx>=0&&dx<640)kdialog_pixel(p->image.pixels+y*p->image.stride+dx*4,color);
        }
        config_stamp(p,0,188,8,128,4,495,287);
        config_stamp(p,0,148,12,12,12,493,276);config_stamp(p,0,160,12,12,12,612,276);
        config_stamp(p,0,184,0,4,12,495+p->setting_values[0],283);
        config_toggle(p,b,2,334,16);config_toggle(p,b,3,390,17);
        int on=p->setting_values[17];config_stamp(p,0,292,on?172:176,108,4,408,399);
        config_stamp(p,0,148,12,12,12,407,388);config_stamp(p,0,160,12,12,12,504,388);
        if(on)config_stamp(p,0,184,0,4,12,408+p->setting_values[1],395);
    }else if(page==1){
        const int titles[][3]={{44,0,24},{116,24,24},{160,48,24},{248,72,28},{308,100,24},{352,124,28}};
        for(unsigned i=0;i<6;i++)config_stamp(p,1,0,titles[i][1],112,titles[i][2],316,titles[i][0]);
        const int lines[]={84,140,220,276,332,412};for(unsigned i=0;i<6;i++)config_line(p,lines[i]);
        config_toggle(p,b,4,63,6);config_toggle(p,b,5,118,7);
        config_toggle(p,b,6,254,5);config_toggle(p,b,7,198,19);config_toggle(p,b,8,310,KSET_SUNGEKI_SE);
        config_audio_slider(p,b,4,72,68,61);config_audio_slider(p,b,5,127,123,116);
        config_audio_slider(p,b,6,263,259,252);config_audio_slider(p,b,7,207,203,196);
        config_stamp(p,1,112,48+20*config_state(p,b,9*256,0),152,20,375,390);
    }else{
        const int labels[][2]={{60,0},{108,24},{200,72},{256,96},{312,120}};
        for(unsigned i=0;i<5;i++)config_stamp(p,2,184,labels[i][1],172,24,316,labels[i][0]);
        config_stamp(p,2,184,144,256,28,344,136);config_stamp(p,2,184,172,256,28,344,340);
        const int lines[]={84,176,224,280,352};for(unsigned i=0;i<5;i++)config_line(p,lines[i]);
        config_three(p,b,10,8,0,495,62);
        for(int value=0;value<2;value++)config_stamp(p,0,224,28+(1-value)*80+20*config_state(p,b,10*256+value+3,p->setting_values[21]==value),64,20,561-value*48,110);
        config_toggle(p,b,11,154,KSET_MOVIE_VSYNC);config_toggle(p,b,16,200,KSET_SUNGEKI_ANIME);
        config_toggle(p,b,17,256,KSET_SCHEDULE_CHECK);config_three(p,b,18,KSET_MINIGAME_DIFFICULTY,52,491,312);
    }
}
static void settings_panel_draw_at(MessagePanel *p,KBootstrap *b,SDL_Renderer *r,uint64_t now){
    settings_panel_init(p,b);
    config_motion_tick(p,now);if(p->kind!=8)return;
    p->config_dx=p->config_dy=p->config_preview_dx=0;p->config_alpha=255;
    if(ui_canvas(&p->image))return;
    if(config_assets(p,b)){
        ui_scale(&p->image,&b->layers[0],0,0,640,480);
        ui_text(b,&p->image,"設定画面の素材を読み込めませんでした\nB / Esc：戻る",120,210,400,60,0xffffff,0);
        ui_present(r,&p->image,&p->texture);return;
    }
    unsigned page=p->setting_page==3?1:p->setting_page;
    unsigned motion=p->config_motion,n=p->config_step,steps=p->config_steps?p->config_steps:1;
    int entering=motion==1,exiting=motion==4;
    unsigned opacity=entering?255*n/steps:255;
    /* 4c47c0: black alpha192; text preview alone has a hole in the shade. */
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        const uint8_t *s=b->layers[0].pixels+y*b->layers[0].stride+x*4;uint8_t *d=p->image.pixels+y*p->image.stride+x*4;
        int hole=page==0&&!motion&&x>=350&&x<635&&y>=186&&y<208;
        unsigned shade=192*opacity/255;
        for(unsigned c=0;c<3;c++)d[c]=hole?s[c]:(uint8_t)(s[c]*(255-shade)/255);
        d[3]=255;
    }
    p->config_alpha=exiting?255-255*n/steps:opacity;
    config_stamp(p,0,0,0,116,28,3,2);p->config_alpha=255;
    p->config_dx=entering||motion==3?600-600*(int)n/(int)steps:exiting||motion==2?600*(int)n/(int)steps:0;
    p->config_preview_dx=entering?640-640*(int)n/(int)steps:p->config_dx;
    config_draw_page(p,b,page);p->config_dx=0;
    /* Native sprite priorities: right tabs and Cancel are behind left tabs/OK. */
    p->config_dy=entering?-20+20*(int)n/(int)steps:exiting?-20*(int)n/(int)steps:0;
    for(int i=2;i>=0;i--)config_stamp(p,0,112,108+i*80+20*config_state(p,b,14*256+i,page==(unsigned)i),112,20,370+i*86,0);
    p->config_dy=0;p->config_dx=entering?-112+112*(int)n/(int)steps:exiting?-112*(int)n/(int)steps:0;p->config_alpha=opacity;
    for(int i=3;i<6;i++)config_stamp(p,0,i==5?112:0,(i==3?268:i==4?348:28)+20*config_state(p,b,15*256+i,0),112,20,0,312+i*28);
    p->config_dx=0;p->config_alpha=255;p->config_dy=entering?32-32*(int)n/(int)steps:exiting?32*(int)n/(int)steps:0;
    config_stamp(p,0,244,316+32*config_state(p,b,12*256,0),130,32,510,448);
    config_stamp(p,0,224,188+32*config_state(p,b,13*256+2,0),132,32,400,448);
    config_stamp(p,0,112,348+32*config_state(p,b,13*256,0),132,32,290,448);
    p->config_dy=0;
    if(p->setting_page==3){
        p->config_alpha=motion==5?255*n/steps:motion==6?255-255*n/steps:255;
        config_stamp(p,4,0,0,428,436,108,24);
        for(int i=0;i<33;i++){
            int col=i/17,row=i%17;
            config_stamp(p,3,col*76,row*20,76,20,124+col*208,40+row*24);
            for(int value=0;value<2;value++)config_stamp(p,0,224,28+(1-value)*80+20*config_state(p,b,(100+i)*256+value,p->setting_values[KSET_CHARACTER_VOICE+i]==value),64,20,248+col*208-value*48,40+row*24);
        }
        config_stamp(p,3,152,20*config_state(p,b,200*256,0),120,20,350,424);
        config_stamp(p,3,272,20*config_state(p,b,201*256,0),64,20,456,424);
    }
    p->config_alpha=255;
    if(p->status[0])ui_text(b,&p->image,p->status,124,32,492,40,0xffd060,0);
    ui_present(r,&p->image,&p->texture);
}
static void settings_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){settings_panel_draw_at(p,b,r,SDL_GetTicks64());}

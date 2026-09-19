/* Finger events only: taps activate on release, holds cancel, drags move controls.
   Menu navigation never routes through a mouse cursor.
   A contact belongs to the panel where it began; changing panels cannot
   turn its remaining motion/up events into a second activation. */
typedef struct {SDL_FingerID finger;unsigned active,owner,drag,moved;int x,y,last_x,last_y;uint64_t key,started;const KBootstrap *runtime;} MenuTouch;
static void game_cancel(SaveMenu *,MessagePanel *,KBootstrap *);
static void save_menu_touch(SaveMenu *m,KBootstrap *b,int x,int y){
    if(m->loading){if(y>=302&&y<355)save_menu_action(m,b,1);return;}
    if(m->overwrite){
        if(y>=254&&y<288)save_menu_action(m,b,6);
        else if(y>=310&&y<332)save_menu_action(m,b,x<320?0:1);
        else if(y>=332&&y<357)save_menu_action(m,b,x<320?6:7);
        return;
    }
    if(x>=366&&x<638&&y>=35&&y<475){m->focus=0;m->slot=(m->slot-1)/10*10+(y-35)/44+1;save_menu_action(m,b,0);}
    else if(x>=164&&x<600&&y>=4&&y<24){m->slot=(x-164)/44*10+(m->slot-1)%10+1;save_menu_refresh(m,bootstrap_save_dir(b));}
    else if(x>=604&&y>=4&&y<24)save_menu_action(m,b,1);
    else if(x>=12&&x<160&&y>=316&&y<470){m->focus=1;m->button=(y-316)/26;save_menu_action(m,b,0);}
    else if(x>=176&&x<352&&y>=316&&y<366)save_menu_action(m,b,7);
}
static void settings_touch_value(MessagePanel *p,unsigned item,int x){
    const SettingsControl *c=&settings_controls[item];int limit=bootstrap_setting_limit(c->item),value;
    if(limit>2){value=(x-c->x)*255/(c->w-1);if(value<0)value=0;if(value>255)value=255;}
    else value=limit==2?(x-c->x)/80:c->item==22?x-c->x>=88:x-c->x<88;
    p->selected=item;p->setting_values[c->item]=value;
    if(c->item>=16&&c->item<=17&&value)p->setting_values[c->item==16?17:16]=0;
}
static void settings_touch(MessagePanel *p,KBootstrap *b,MenuTouch *touch,int x,int y){
    settings_panel_init(p,b);
    if(y>=4&&y<24&&x>=340){if(x>=604)settings_panel_action(p,b,1);else{p->setting_page=(unsigned)(x-340)/88;for(unsigned i=0;i<sizeof(settings_controls)/sizeof(*settings_controls);i++)if(settings_controls[i].page==p->setting_page){p->selected=i;break;}}return;}
    if(y>=438&&y<474&&x>=376&&x<624){settings_panel_action(p,b,x<460?0:x<540?1:7);return;}
    if(x>=12&&x<160&&y>=316&&y<470){unsigned button=(unsigned)(y-316)/26;p->setting_ready=0;
        if(button<2){p->kind=0;b->message_request=button?3:2;}else{p->kind=button==2?5:button==3?8:button==4?18:6;p->back=p->viewing=0;}return;}
    for(unsigned i=0;i<sizeof(settings_controls)/sizeof(*settings_controls);i++){const SettingsControl *c=&settings_controls[i];
        if(c->page==p->setting_page&&x>=c->x-6&&x<c->x+c->w+6&&y>=c->y-4&&y<c->y+c->h+4){
            int clamped=x<c->x?c->x:x>=c->x+c->w?c->x+c->w-1:x;settings_touch_value(p,i,clamped);if(bootstrap_setting_limit(c->item)>2)touch->drag=i+1;return;
        }
    }
}
static void backlog_touch(MessagePanel *p,KBootstrap *b,MenuTouch *touch,int x,int y){
    if(x>=600){if(y<32)message_panel_action(p,b,2);else if(y>=448)message_panel_action(p,b,3);else{touch->drag=100;if(b->history_count){history_voice_stop(p);p->back=(unsigned)(448-y)*(b->history_count-1)/416;}}return;}
    if(p->viewing){if(y>=420){message_panel_action(p,b,6);return;}history_voice_play(p,b);return;}
    unsigned first=p->back/6*6,count=b->history_count>first?b->history_count-first:0;if(count>6)count=6;unsigned row=(unsigned)y/80;if(row>=count)return;
    history_voice_stop(p);p->back=first+count-1-row;p->status[0]=0;
    if(x<40)history_voice_play(p,b);else message_panel_action(p,b,6);
}
static void navigation_touch(MessagePanel *p,KBootstrap *b,int x,int y){
    navigation_panel_refresh(p,b);y-=178;if(y<0||y>=118)return;
    if(x<512){unsigned item=p->variant/4*4+(unsigned)x/128;if(item<navigation_panel_count(p)){p->variant=item;navigation_panel_action(p,b,0);}return;}
    if(y>=16&&y<36)navigation_panel_action(p,b,x<572?8:9);
    else if(y>=38&&y<80){unsigned mode=y>=60;if(mode!=p->nav_mode)navigation_panel_action(p,b,6);}
    else if(y>=82&&y<104)navigation_panel_action(p,b,1);
}
static void gallery_touch(MessagePanel *p,KBootstrap *b,int x,int y){
    if(p->viewing>=2){message_panel_action(p,b,0);return;}
    if(!p->viewing){
        if(x>=560&&x<608&&y>=24&&y<388){unsigned group=(unsigned)(y-24)/52;if(group<7&&(y-24)%52<48){p->back=group;p->selected=0;}return;}
        if(x>=532&&x<628&&y>=388&&y<440){message_panel_action(p,b,1);return;}
        if(x>=420&&x<528&&y>=340&&y<440){message_panel_action(p,b,y<388?8:9);return;}
        KGalleryThumbnail page;if(kgallery_thumbnail(&b->gallery,b->vm->bytes,b->vm->byte_count,p->back,p->selected,&page))return;
        for(unsigned i=0;i<page.count;i++){KGalleryThumbnail t;if(kgallery_thumbnail(&b->gallery,b->vm->bytes,b->vm->byte_count,p->back,page.first+i,&t))continue;
            if(x>=(int)t.x&&x<(int)t.x+120&&y>=(int)t.y&&y<(int)t.y+90){p->selected=page.first+i;message_panel_action(p,b,0);return;}}
    }else{
        unsigned total=gallery_variant_count(p,b),first=p->variant/15*15,count=total-first;if(count>15)count=15;int special=p->back==5&&p->selected==45;
        int ox=special?64:count<5?(5-(int)count)*64:0,oy=special?82:count<=5?142:count<=10?82:12;
        if(y>=424&&y<452){if(x>=208&&x<304)message_panel_action(p,b,x<256?8:9);else if(x>=328&&x<424)message_panel_action(p,b,1);return;}
        for(unsigned i=0;i<count;i++){unsigned base=special?i%4:i;int nx=ox+(int)(base%5)*128,ny=oy+(int)(base/5)*130+(special&&i>=4?130:0);
            if(x>=nx+4&&x<nx+124&&y>=ny+14&&y<ny+104){p->variant=first+i;message_panel_action(p,b,0);return;}}
    }
}
static void panel_touch(MessagePanel *p,KBootstrap *b,MenuTouch *touch,int x,int y){
    if(p->kind==8){settings_touch(p,b,touch,x,y);return;}
    if(p->kind==5){backlog_touch(p,b,touch,x,y);return;}
    if(p->kind==16){navigation_touch(p,b,x,y);return;}
    if(p->kind==10){gallery_touch(p,b,x,y);return;}
    if(p->kind==6||p->kind==18||p->kind==15){int top=p->kind==15?336:278;if(y>=top&&y<top+34)message_panel_action(p,b,x<320?0:1);return;}
    if(p->kind==14){if(y>=242&&y<267&&x>=200&&x<443)message_panel_action(p,b,x<272?7:x<358?0:1);else if(y>=206&&y<232&&x>=186&&x<454)message_panel_action(p,b,6);return;}
    if(p->kind==9){
        if(x>=24&&x<256&&y>=20&&y<468){unsigned i=(unsigned)(y-20)/28;if((y-20)%28<20&&i<16){p->selected=i;message_panel_action(p,b,0);}}
        if(y>=420&&y<448){if(x>=340&&x<460)message_panel_action(p,b,6);else if(x>=484&&x<556)message_panel_action(p,b,1);}return;
    }
    if(p->kind==12){if(y>=236&&y<264&&((x>=236&&x<308)||(x>=340&&x<412))){p->selected=x>=340;message_panel_action(p,b,0);}return;}
    if(p->kind==11){
        if(x>=560&&x<608&&y>=48&&y<328){unsigned group=(unsigned)(y-48)/56;if(group<5&&(y-48)%56<48){p->back=group;p->selected=0;}return;}
        if(x>=532&&x<628&&y>=388&&y<440){message_panel_action(p,b,1);return;}
        const unsigned (*positions)[2]=replay_positions[p->back<3?0:p->back-2];
        for(unsigned i=0;i<bootstrap_replay_count(p->back);i++)if(x>=(int)positions[i][0]&&x<(int)positions[i][0]+120&&y>=(int)positions[i][1]&&y<(int)positions[i][1]+90){p->selected=i;message_panel_action(p,b,0);return;}
        return;
    }
    if(p->kind==13){
        for(unsigned row=0;row<2;row++)for(unsigned col=0;col<20;col++)if(x>=(int)nawa_x[col]&&x<(int)(nawa_x[col]+nawa_w[col])&&y>=27+(int)row*140&&y<(row==1&&col==19?264:146+(int)row*140)){p->selected=row*20+col;message_panel_action(p,b,0);return;}
        unsigned item=99;if(x>=564&&x<637&&y>=222&&y<363)item=40;else if(x>=14&&x<119&&y>=210&&y<367)item=41;else if((x>=535&&x<544&&y>=244&&y<266)||(x>=515&&x<563&&y>=265&&y<325))item=42;
        if(item<43){p->selected=item;message_panel_action(p,b,0);}return;
    }
    if(p->kind==4&&b->scene&&p->selected<b->scene->count){
        KScene *s=b->scene;const KSceneNode *focus=&s->nodes[p->selected];int left=focus->x-290,top=focus->y-219;if(left<0)left=0;if(top<0)top=0;
        int maxx=1,maxy=1;for(unsigned i=0;i<s->count;i++)if(s->nodes[i].width){if(s->nodes[i].x+60>maxx)maxx=s->nodes[i].x+60;if(s->nodes[i].y+42>maxy)maxy=s->nodes[i].y+42;}
        int best=100000;unsigned selected=p->selected;
        for(unsigned i=0;i<s->count;i++){const KSceneNode *n=&s->nodes[i];if(!n->width)continue;
            int nx=p->viewing?16+n->x*608/maxx:n->x-left,ny=p->viewing?16+n->y*448/maxy:n->y-top;
            int w=p->viewing?14:60,h=p->viewing?5:42,dx=x-nx-w/2,dy=y-ny-h/2,d=dx*dx+dy*dy;
            if((p->viewing||(x>=nx&&x<nx+w&&y>=ny&&y<ny+h))&&d<best){best=d;selected=i;}}
        if(best<100000){p->selected=selected;if(!p->viewing)message_panel_action(p,b,0);}return;
    }
}
static uint64_t touch_owner_key(const SaveMenu *m,const MessagePanel *p,const KBootstrap *b){
    if(p->kind){unsigned state[]={p->kind,p->viewing,p->back,p->setting_page};return ui_hash(1,state,sizeof(state));}
    if(m->active){int state[]={m->save,m->slot,m->overwrite,m->loading};return ui_hash(2,state,sizeof(state));}
    unsigned state[]={b->message_active,b->message_user_hidden,b->text_count,b->choice_active,b->title.active,b->title.extra,b->flag_dialog.active,b->area_active,b->bonus52_active};
    return ui_hash(3,state,sizeof(state));
}
static void touch_game_tap(KBootstrap *b,int x,int y){
    if(b->message_active&&b->message_user_hidden){bootstrap_confirm(b);return;}
    if(b->area_active||b->bonus52_active||b->choice_active||b->title.active||b->flag_dialog.active||b->message_active)bootstrap_pointer(b,x,y,1);
    else bootstrap_confirm(b);
}
static int menu_touch_event(MenuTouch *touch,SaveMenu *m,MessagePanel *p,KBootstrap *b,const SDL_Event *e,uint64_t now){
    if(e->type!=SDL_FINGERDOWN&&e->type!=SDL_FINGERMOTION&&e->type!=SDL_FINGERUP)return 0;
    float fx=e->tfinger.x,fy=e->tfinger.y;
    if(!isfinite(fx)||!isfinite(fy)||fx< -1||fx>2||fy< -1||fy>2)return 1;
    int x=(int)((fx*1280-160)*2/3),y=(int)(fy*480);
    unsigned owner=p->kind?p->kind:m->active?100:0;
    if(e->type==SDL_FINGERDOWN){
        if(touch->active)return 1;
        touch->active=1;touch->finger=e->tfinger.fingerId;touch->owner=owner;touch->drag=touch->moved=0;
        touch->key=touch_owner_key(m,p,b);touch->runtime=b;touch->started=now;touch->x=touch->last_x=x;touch->y=touch->last_y=y;
        if(fx<0.125f||fx>=0.875f||fy<0||fy>=1){touch->owner=999;return 1;}
        if(owner==8){settings_panel_init(p,b);for(unsigned i=0;i<sizeof(settings_controls)/sizeof(*settings_controls);i++){const SettingsControl *c=&settings_controls[i];
            if(c->page==p->setting_page&&bootstrap_setting_limit(c->item)>2&&x>=c->x-6&&x<c->x+c->w+6&&y>=c->y-4&&y<c->y+c->h+4){touch->drag=i+1;settings_touch_value(p,i,x);break;}}}
        else if(owner==5&&x>=600&&y>=32&&y<448){touch->drag=100;backlog_touch(p,b,touch,x,y);}
        else if(!owner&&(b->area_active||b->bonus52_active))bootstrap_pointer(b,x,y,0);
        /* Opening a draft or positioning its scrollbar is part of this contact. */
        touch->key=touch_owner_key(m,p,b);return 1;
    }
    if(!touch->active||touch->finger!=e->tfinger.fingerId)return 1;
    int valid=touch->runtime==b&&touch->owner==owner&&touch->key==touch_owner_key(m,p,b);
    if(e->type==SDL_FINGERMOTION){
        if(!valid)return 1;
        int dx=x-touch->last_x,dy=y-touch->last_y;
        if(abs(x-touch->x)>12||abs(y-touch->y)>12)touch->moved=1;
        if(owner==8&&touch->drag&&touch->drag<100)settings_touch_value(p,touch->drag-1,x);
        else if(owner==5&&touch->drag==100&&b->history_count){int py=y<32?32:y>448?448:y;history_voice_stop(p);p->back=(unsigned)(448-py)*(b->history_count-1)/416;}
        else if(owner==10&&p->viewing==2&&p->cg_zoom&&touch->moved){
            p->cg_scroll_active=0;p->cg_x-=dx;p->cg_y-=dy;int maxx=p->image.width>640?(int)p->image.width-640:0,maxy=p->image.height>480?(int)p->image.height-480:0;
            if(p->cg_x<0)p->cg_x=0;
            if(p->cg_x>maxx)p->cg_x=maxx;
            if(p->cg_y<0)p->cg_y=0;
            if(p->cg_y>maxy)p->cg_y=maxy;
        }else if(!owner&&b->bonus52_active){bootstrap_pointer(b,x,y,0);touch->drag=101;}
        else if(!owner&&b->area_active)bootstrap_pointer(b,x,y,0);
        touch->last_x=x;touch->last_y=y;touch->key=touch_owner_key(m,p,b);return 1;
    }
    touch->active=0;
    if(!valid||touch->owner==999)return 1;
    if(touch->drag)return 1;
    int dx=x-touch->x,dy=y-touch->y;
    if(touch->moved||abs(dx)>12||abs(dy)>12){
        if(owner==5&&abs(dy)>24){unsigned steps=(unsigned)abs(dy)/40+1;while(steps--)message_panel_action(p,b,dy>0?2:3);}
        else if(!owner&&b->message_active&&dy< -48){p->kind=5;p->back=p->viewing=0;p->native_kind=0;}
        else if(abs(dx)>64&&abs(dx)>abs(dy)){
            if(owner==100)save_menu_action(m,b,dx<0?9:8);
            else if(owner==8||owner==16||(owner==10&&p->viewing<2))message_panel_action(p,b,dx<0?9:8);
            else if(owner==11)message_panel_action(p,b,dx<0?6:7);
        }return 1;
    }
    if(now-touch->started>=600){if(owner==100)save_menu_action(m,b,1);else if(owner)message_panel_action(p,b,1);else game_cancel(m,p,b);return 1;}
    if(fx<0.125f||fx>=0.875f||fy<0||fy>=1)return 1;
    if(owner==100)save_menu_touch(m,b,x,y);else if(owner)panel_touch(p,b,touch,x,y);else touch_game_tap(b,x,y);
    return 1;
}

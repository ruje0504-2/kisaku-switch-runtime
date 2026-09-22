/* Controller hints share the CName left-margin anchor. Cache glyph rendering
   and upload only when the controls change, not once per game frame. */
typedef struct {KImage image;SDL_Texture *texture;char text[512];unsigned replay;} ControlsOverlay;
static void controls_focus(SDL_Renderer *r,int x,int y,int w,int h){
    SDL_Rect box={160+x*3/2,y*3/2,w*3/2,h*3/2};
    for(int i=0;i<5;i++){
        SDL_SetRenderDrawColor(r,i==0||i==4?0:255,i==0||i==4?0:224,i==0||i==4?0:32,255);
        SDL_Rect edge={box.x+i,box.y+i,box.w-i*2,box.h-i*2};SDL_RenderDrawRect(r,&edge);
    }
}
static const char *controls_text(const MessagePanel *p,const SaveMenu *m,const KBootstrap *b){
    if(m->active&&m->live_character)return "左スティック：\n左右回転\n操作後は自動停止\n十字：項目選択\nA：詳細\nB：戻る";
    if(m->active)return m->character_detail?"十字：項目選択\nA：詳細\nB：戻る":"十字：選択\nL/R：ページ\nA：決定\nB：戻る";
    switch(p->kind){
    case 14:return NULL; /* CName uses this same anchor and its own cache. */
    case 20:return scene_mode_hage((KBootstrap *)b)||p->direct_sub>=0?"十字：選択\nL/R：ページ\nA：再生\nB：戻る":"十字：選択\nX：前の人物\nY：次の人物\nL/R：ページ\nA：再生\nB：戻る";
    case 22:return !bootstrap_native_cg_categories(b)?"十字：選択\nL/R：ページ\nA：決定／次へ\nB：戻る":"十字：選択\nX：前の人物\nY：次の人物\nL/R：ページ\nA：決定／次へ\nB：戻る";
    case 10:if(p->viewing==2)return "十字：移動\nA/Y：拡大縮小\nX：スクロール\nB：戻る";
        if(p->viewing)return "十字：選択\nL/R：ページ\nA：決定\nB：戻る";
        /* fall through */
    case 11:return "十字：選択\nX：前の人物\nY：次の人物\nL/R：ページ\nA：決定\nB：戻る";
    case 9:return "十字：曲選択\nA：再生\nY：停止\nB：戻る";
    case 23:return "十字：動画選択\nA：再生\nB：戻る";
    case 5:return "十字：履歴選択\nL/R：ページ\nA：音声再生\nB：戻る";
    case 8:return "十字：選択／調整\nL/R：ページ\nA：決定\nB：戻る";
    case 4:return "十字：移動\nA：場面選択\nY：拡大縮小\nB：戻る";
    case 16:return "十字：選択\nL/R：ページ\nY：切替\nA：決定\nB：戻る";
    default:if(p->kind)return "十字：選択\nA：決定\nB：戻る";
    }
    if(b->area_active)return "十字：対象選択\nA：調べる／使用\nB：戻る／道具";
    if(b->bonus52_active)return "スティック移動\nA：連打";
    if(b->title.active||b->choice_active)return "十字：選択\nA：決定";
    if(b->scene_replay)return "A：次へ\nB：メニュー";
    return NULL;
}
static void controls_overlay_draw(ControlsOverlay *c,MessagePanel *p,SaveMenu *m,KBootstrap *b,SDL_Renderer *r){
    const char *text=controls_text(p,m,b);
    if(c->replay&&!p->kind&&!m->active)text="A：次へ\nZL：本編に戻る";
    if(text){
        if(strcmp(c->text,text)){
            SDL_DestroyTexture(c->texture);c->texture=NULL;rmt_free(&c->image);c->text[0]=0;
            c->image=(KImage){0,0,144,164,576,calloc(144u*164u,4)};
            if(c->image.pixels){ui_fill(&c->image,0,0,144,164,0x0c0f14);
                if(!ui_text(b,&c->image,text,0,0,144,164,0xffe9a0,0)){
                    c->texture=kimage_texture(r,&c->image);if(c->texture)snprintf(c->text,sizeof(c->text),"%s",text);
                }
            }
        }
        if(c->texture){SDL_Rect dst={8,420,144,164};SDL_RenderCopy(r,c->texture,NULL,&dst);}
    }
    /* The presentation-layer focus frame is only wanted where the native
       cursor is hard to see: the settings menu and the two gallery
       interfaces (CG images and scene selection).  Every other screen keeps
       its own native cursor, so no frame is drawn there. */
    if(p->kind==8&&!p->config_motion&&p->setting_page<4&&p->selected<p->config_area_count[p->setting_page]){
        const ConfigArea *a=&p->config_areas[p->setting_page][p->selected];controls_focus(r,a->x0,a->y0,a->x1-a->x0,a->y1-a->y0);
    }
    else if(p->kind==22){int a[4];if(bootstrap_native_cg_focus(b,a))controls_focus(r,a[0],a[1],a[2],a[3]);}
    else if(p->kind==20&&!p->direct_motion){
        int f=p->direct_focus,x,y;unsigned cat,item;scene_mode_at(b,p->direct_selected,&cat,&item);
        if(f>=0&&f<9){scene_mode_grid(p,b,(unsigned)f,&x,&y);controls_focus(r,x-5,y-4,160,120);}
        else if(f>=0x100&&f<0x200)controls_focus(r,0,172+(f-0x100)*28,112,20);
        else if(f==0x300)controls_focus(r,528,448,112,32);
        else if(f>=0x200&&f<0x300){unsigned pages=(scene_mode_items(b,cat)+8)/9;int hage=scene_mode_hage(b);controls_focus(r,(hage?460:498)+(f-0x200+(hage?4:3)-(int)pages)*38,0,48,20);}
    }
}

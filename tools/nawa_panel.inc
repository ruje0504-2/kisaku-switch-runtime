/* 47c7d0/47d170/47d480: native Nawatsuna menu resources and private AX
   destinations. These surfaces never alias the story's image pages. */
struct KNawaPanel {
    KImage sheet,planes[3];struct ax_player ax;
    uint8_t preview_alpha[240*180];
    SDL_Texture *background,*parts,*balloon,*sprites[3];
    unsigned selected,dirty,error,preview_state;uint64_t clock,preview_clock,preview_start;
    KMov mov;KVideo *video;uint8_t *commands,*video_data,*pcm;size_t pcm_size;KImage preview;SDL_Texture *preview_texture;
};
/* A 36-pixel easing band softens the original mask and reaches zero at
   the rectangle boundary. Calculated only when the menu is opened. */
static double nawa_edge_weight(unsigned distance){
    double t=distance/36.0;if(t>=1)return 1;
    return t*t*(3-2*t);
}
static const unsigned nawa_x[20]={121,141,162,183,202,224,245,265,286,306,327,348,368,389,410,430,451,472,492,513};
static const unsigned nawa_sx[20]={0,21,43,65,86,108,130,151,173,194,216,238,259,281,303,324,346,368,389,411};
static const unsigned nawa_w[20]={21,22,22,21,22,22,21,22,21,22,22,21,22,22,21,22,22,21,22,22};
static const char *const nawa_movies[40]={"ev196d.mov","ev200g.mov","ev204.mov","ev254.mov","ev215c.mov","ev216u.mov","ev220k.mov","ev229b.mov","ev229i.mov","ev107a.mov","ev149j.mov","ev151c.mov","ev189h.mov","ev223h.mov","ev223i.mov","ev223j.mov","ev88.mov","ev119d.mov","ev30b.mov","ev131c.mov","ev191g.mov","ev191i.mov","ev191j.mov","ev205c.mov","ev213i.mov","ev227d.mov","ev94i.mov","ev95b.mov","ev99d.mov","ev131a.mov","ev132a.mov","ev23a.mov","ev74b.mov","ev102g.mov","ev115b.mov","ev116a.mov","ev164.mov","ev170b.mov","ev177l.mov","ev165.mov"};
static void nawa_preview_stop(KNawaPanel *n){
    kvideo_close(n->video);n->video=NULL;free(n->commands);n->commands=NULL;free(n->video_data);n->video_data=NULL;free(n->pcm);n->pcm=NULL;n->pcm_size=0;
    rmt_free(&n->preview);SDL_DestroyTexture(n->preview_texture);n->preview_texture=NULL;n->preview_state=0;memset(&n->mov,0,sizeof(n->mov));
}
static int nawa_resource(Ai6Archive *arc,const char *name,uint8_t **data,size_t *size){
    /* Override-aware: loose patch files win over the archive entry. */
    return ai6_read_named(arc,name,data,size);
}
static int nawa_preview_next(KNawaPanel *n){
    for(unsigned guard=0;guard<1000;guard++){
        int event=kmov_next(&n->mov);if(event<0)return -1;if(!event){n->preview_state=2;return 1;}
        if(event==1)return kvideo_range(n->video,n->mov.first,n->mov.last)?-1:0;
        /* Hover previews are silent; selecting A enters the full replay. */
    }return -1;
}
static int nawa_preview_open(KNawaPanel *n,KBootstrap *b,unsigned item,uint64_t now){
    size_t size=0;
    if(item>=40||nawa_resource(&b->movies,nawa_movies[item],&n->commands,&size)||kmov_open(&n->mov,n->commands,size,0)||nawa_resource(&b->movies,n->mov.video,&n->video_data,&size))goto bad;
    n->video=kvideo_open(n->video_data,size);if(!n->video)goto bad;
    n->preview=(KImage){0,0,240,180,960,calloc(240*180,4)};if(!n->preview.pixels)goto bad;
    n->preview_state=1;n->preview_clock=now*60;
    if(nawa_preview_next(n)||kvideo_step(n->video,&n->preview,&n->pcm,&n->pcm_size))goto bad;
    return 0;
bad:nawa_preview_stop(n);n->preview_state=3;return -1;
}
static int nawa_preview_tick(KNawaPanel *n,SDL_Renderer *r,uint64_t now){
    unsigned changed=!n->preview_texture;
    if(n->preview_clock>now*60||now*60-n->preview_clock>6000)n->preview_clock=now*60;
    while(n->preview_state==1&&now*60-n->preview_clock>=1000){
        n->preview_clock+=1000;int rc=kvideo_step(n->video,&n->preview,&n->pcm,&n->pcm_size);
        if(rc<0)return -1;
        if(rc==1){rc=nawa_preview_next(n);if(rc<0)return -1;if(!rc&&kvideo_step(n->video,&n->preview,&n->pcm,&n->pcm_size))return -1;}
        changed=1;
    }
    if(changed){
        /* 47d730 seeds the atlas alpha; native video blit 49341a uses
           copy-alpha=0 (43f623), preserving this feather on every frame. */
        for(unsigned y=0;y<180;y++)for(unsigned x=0;x<240;x++)
            n->preview.pixels[y*n->preview.stride+x*4+3]=n->preview_alpha[y*240+x];
        if(!n->preview_texture){n->preview_texture=kimage_texture(r,&n->preview);if(n->preview_texture){SDL_SetTextureBlendMode(n->preview_texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(n->preview_texture,SDL_ScaleModeLinear);}}
        else if(SDL_UpdateTexture(n->preview_texture,NULL,n->preview.pixels,(int)n->preview.stride))return -1;
        if(!n->preview_texture)return -1;
    }return 0;
}
static void nawa_panel_clear(MessagePanel *p){
    KNawaPanel *n=p->nawa;if(!n)return;
    nawa_preview_stop(n);
    rmt_free(&n->sheet);SDL_DestroyTexture(n->background);SDL_DestroyTexture(n->parts);SDL_DestroyTexture(n->balloon);
    for(unsigned i=0;i<3;i++){rmt_free(&n->planes[i]);SDL_DestroyTexture(n->sprites[i]);}
    free(n);p->nawa=NULL;
}
static void nawa_ax_draw(const uint32_t d[7],unsigned cell,void *context){
    KNawaPanel *n=context;
    if(d[0]==2)return;
    if(d[0]!=3||cell>=3){n->error=1;return;}
    KImage *dst=&n->planes[cell],*src=&n->sheet;
    if(d[1]>src->width||d[2]>src->height||d[3]>src->width-d[1]||d[4]>src->height-d[2]||d[5]>dst->width||d[6]>dst->height||d[3]>dst->width-d[5]||d[4]>dst->height-d[6]){n->error=1;return;}
    for(unsigned y=0;y<d[4];y++)memcpy(dst->pixels+(d[6]+y)*dst->stride+d[5]*4,src->pixels+(d[2]+y)*src->stride+d[1]*4,d[3]*4);
    n->dirty|=1u<<cell;
}
static int nawa_panel_open(MessagePanel *p,KBootstrap *b,SDL_Renderer *r,uint64_t now){
    KNawaPanel *n=calloc(1,sizeof(*n));if(!n)return -1;p->nawa=n;n->selected=~0u;n->clock=now;
    n->background=gallery_panel_texture(b,r,"nawatuna.rmt",640,480,0);
    n->parts=gallery_panel_texture(b,r,"nawatuna_p.rmt",676,536,1);
    n->balloon=gallery_panel_texture(b,r,"balloon_p.rmt",172,276,1);
    uint8_t *data=NULL;size_t size=0;unsigned i=0;
    KImage mask={0};
    if(nawa_resource(&b->images,"nawatuna_p.rmt",&data,&size))goto bad;
    int mask_rc=rmt_decode(data,size,&mask);free(data);data=NULL;
    if(mask_rc||mask.width<676||mask.height<180){rmt_free(&mask);goto bad;}
    for(unsigned y=0;y<180;y++)for(unsigned x=0;x<240;x++){
        unsigned dx=x<239-x?x:239-x,dy=y<179-y?y:179-y;
        double weight=nawa_edge_weight(dx)*nawa_edge_weight(dy);
        unsigned eased=(unsigned)(255*weight+0.5),original=mask.pixels[y*mask.stride+(436+x)*4+3];
        n->preview_alpha[y*240+x]=(uint8_t)(eased<original?eased:original);
    }
    rmt_free(&mask);
    if(!n->background||!n->parts||!n->balloon||ai6_read_named(&b->images,"chibian.rmt",&data,&size))goto bad;
    int rc=rmt_decode(data,size,&n->sheet);free(data);data=NULL;if(rc)goto bad;
    if(ai6_read_named(&b->data,"chibi.ax",&data,&size))goto bad;
    rc=ax_load(&n->ax,"chibi.ax",data,size);free(data);data=NULL;if(!rc)goto bad;
    for(i=0;i<3;i++){
        n->planes[i]=(KImage){0,0,640,480,2560,calloc(640*480,4)};
        if(!n->planes[i].pixels)goto bad;
    }
    /* 47d64d..47d6cd supplies static initial poses before hover starts AX. */
    const uint32_t initial[3][7]={{3,0,0,80,152,560,216},{3,0,152,120,168,8,210},{3,72,320,72,112,504,216}};
    for(i=0;i<3;i++)nawa_ax_draw(initial[i],i,n);
    if(n->error)goto bad;
    return 0;
bad:free(data);nawa_panel_clear(p);return -1;
}
static int nawa_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r,uint64_t now){
    if(!p->nawa&&nawa_panel_open(p,b,r,now)){snprintf(p->status,sizeof(p->status),"Nawatsuna menu resources could not be loaded");return -1;}
    KNawaPanel *n=p->nawa;
    if(n->selected!=p->selected){
        if(n->selected>=40&&n->selected<43)n->ax.cells[n->selected-40].state=1;
        if(p->selected>=40&&p->selected<43)n->ax.cells[p->selected-40].state=0;
        n->selected=p->selected;nawa_preview_stop(n);n->preview_start=now;
    }
    /* Limit catch-up after suspension, retaining the native 20 ms cadence. */
    if(now<n->clock||now-n->clock>200)n->clock=now;
    while(now-n->clock>=AX_TICK_MS){n->clock+=AX_TICK_MS;if(!ax_tick(&n->ax,nawa_ax_draw,n))n->error=1;}
    if(n->error){snprintf(p->status,sizeof(p->status),"Nawatsuna animation data invalid");return -1;}
    gallery_panel_copy(r,n->background,0,0,640,480,0,0);
    for(unsigned item=0;item<40;item++)if(bootstrap_nawa_unlocked(b,item)){
        unsigned col=item%20,row=item/20;
        gallery_panel_copy(r,n->parts,nawa_sx[col],(int)(row*2+(item==p->selected))*119,nawa_w[col],119,nawa_x[col],27+row*140);
    }
    for(unsigned i=0;i<3;i++){
        if(n->dirty&(1u<<i)){
            if(!n->sprites[i]){n->sprites[i]=kimage_texture(r,&n->planes[i]);if(!n->sprites[i])return -1;SDL_SetTextureBlendMode(n->sprites[i],SDL_BLENDMODE_BLEND);}
            else if(SDL_UpdateTexture(n->sprites[i],NULL,n->planes[i].pixels,2560))return -1;
        }
        gallery_panel_copy(r,n->sprites[i],0,0,640,480,0,0);
    }
    n->dirty=0;
    if(p->selected<40&&bootstrap_nawa_unlocked(b,p->selected)){
        int shown=0;
        if(!n->preview_state&&now>=n->preview_start&&now-n->preview_start>=200)nawa_preview_open(n,b,p->selected,now);
        if(n->preview_state==1||n->preview_state==2){
            if(nawa_preview_tick(n,r,now)){nawa_preview_stop(n);n->preview_state=3;}
            else {gallery_panel_copy(r,n->preview_texture,0,0,240,180,200,284);shown=1;}
        }
        if(!shown)gallery_panel_copy(r,n->parts,436,0,240,180,200,284);
        if(n->preview_state==3)snprintf(p->status,sizeof(p->status),"Preview unavailable; A still opens the scene");
    }
    const int bx[3]={464,4,412},by[3]={140,136,156},sy[3]={92,0,184};
    for(unsigned i=0;i<3;i++)if(p->selected==40+i)gallery_panel_copy(r,n->balloon,0,sy[i],172,92,bx[i],by[i]);
    if(p->selected<40){unsigned col=p->selected%20,row=p->selected/20;
        SDL_SetRenderDrawColor(r,245,208,80,255);SDL_Rect border={160+(int)nawa_x[col]*3/2, (27+(int)row*140)*3/2,(int)nawa_w[col]*3/2,179};SDL_RenderDrawRect(r,&border);
    }
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,12,18,28,225);SDL_Rect footer={160,696,960,24};SDL_RenderFillRect(r,&footer);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,245,245,245,255);char line[100];
    if(p->selected<40)snprintf(line,sizeof(line),"Scene %02u  %s",p->selected+1,bootstrap_nawa_unlocked(b,p->selected)?"Available":"Locked");
    else snprintf(line,sizeof(line),"Bonus game %u",p->selected-39);
    char footer_text[128];
    if(p->status[0])snprintf(footer_text,sizeof(footer_text),"%.78s",p->status);
    else snprintf(footer_text,sizeof(footer_text),"%.40s",line);
    panel_text(r,176,701,footer_text);return 0;
}

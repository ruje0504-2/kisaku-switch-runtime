/* Original category atlases, backgrounds and highlighted tabs. Cached once
   per page; cursor movement changes only drawing, never reads the archive. */
static void gallery_panel_clear(MessagePanel *p){
    for(unsigned i=0;i<3;i++){SDL_DestroyTexture(p->cg_textures[i]);p->cg_textures[i]=NULL;}p->cg_cached=0;
}
static SDL_Texture *gallery_panel_texture(KBootstrap *b,SDL_Renderer *r,const char *name,unsigned minimum_w,unsigned minimum_h,int alpha){
    uint8_t *data=NULL;size_t size=0;KImage image={0};SDL_Texture *texture=NULL;
    if(ai6_read_named(&b->images,name,&data,&size)||rmt_decode(data,size,&image))goto done;
    if(image.width<minimum_w||image.height<minimum_h)goto done;
    for(unsigned y=0;y<image.height;y++)for(unsigned x=0;x<image.width;x++){
        uint8_t *pixel=image.pixels+y*image.stride+x*4;if(alpha!=1)pixel[3]=alpha==2&&pixel[0]==0&&pixel[1]==255&&pixel[2]==0?0:255;
    }
    texture=kimage_texture(r,&image);if(texture)SDL_SetTextureBlendMode(texture,alpha?SDL_BLENDMODE_BLEND:SDL_BLENDMODE_NONE);
done:free(data);rmt_free(&image);return texture;
}
static void gallery_panel_copy(SDL_Renderer *r,SDL_Texture *texture,int sx,int sy,int w,int h,int x,int y){
    SDL_Rect src={sx,sy,w,h},dst={160+x*3/2,y*3/2,w*3/2,h*3/2};SDL_RenderCopy(r,texture,&src,&dst);
}
static int gallery_panel_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    KGalleryThumbnail current;if(kgallery_thumbnail(&b->gallery,b->vm->bytes,b->vm->byte_count,p->back,p->selected,&current))return -1;
    if(p->cg_cached!=1||p->cg_group!=p->back||p->cg_page!=current.page||p->cg_count!=current.count){
        SDL_Texture *next[3]={gallery_panel_texture(b,r,current.background,640,480,0),
            gallery_panel_texture(b,r,current.atlas,480,90,0),gallery_panel_texture(b,r,"sp_cg_p.rmt",432,616,1)};
        if(!next[0]||!next[1]||!next[2]){for(unsigned i=0;i<3;i++)SDL_DestroyTexture(next[i]);snprintf(p->status,sizeof(p->status),"Thumbnail resources could not be loaded");return -1;}
        gallery_panel_clear(p);memcpy(p->cg_textures,next,sizeof(next));p->cg_cached=1;p->cg_group=p->back;p->cg_page=current.page;p->cg_count=current.count;
    }
    gallery_panel_copy(r,p->cg_textures[0],0,0,640,480,0,0);
    int width=0,height=0;SDL_QueryTexture(p->cg_textures[1],NULL,NULL,&width,&height);
    unsigned seen=0,total=0,images=kgallery_group_count(&b->gallery,p->back);
    for(unsigned i=0;i<current.count;i++){
        KGalleryThumbnail t;if(kgallery_thumbnail(&b->gallery,b->vm->bytes,b->vm->byte_count,p->back,current.first+i,&t))continue;
        unsigned item=current.first+i;
        if(item>=images){total++;seen+=t.tile>=0;}
        else{
            unsigned variants=kgallery_variant_count(&b->gallery,p->back,item),weight=p->back==5&&t.page==3&&t.index==3?2:1;total+=variants*weight;
            if(t.tile>=0)for(unsigned v=0;v<variants;v++){uint16_t layers[3];if(!kgallery_variant(&b->gallery,p->back,item,v,layers))seen+=weight*kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,layers);}
        }
        if(t.tile>=0){int sx=t.tile%4*120,sy=t.tile/4*90;
            if(sx+120>width||sy+90>height)return -1;
            gallery_panel_copy(r,p->cg_textures[1],sx,sy,120,90,(int)t.x,(int)t.y);
        }
        if(i==current.index){
            if(t.tile>=0)gallery_panel_copy(r,p->cg_textures[2],144,250,120,90,(int)t.x,(int)t.y);
            SDL_SetRenderDrawColor(r,245,208,80,255);SDL_Rect border={160+(int)t.x*3/2-2,(int)t.y*3/2-2,184,139};SDL_RenderDrawRect(r,&border);
        }
    }
    for(unsigned group=0;group<7;group++){
        int state=group==p->back?2:0;
        gallery_panel_copy(r,p->cg_textures[2],group==6?288+state*48:state*48,group==6?568:148+(int)group*48,48,48,560,24+(int)group*52);
    }
    gallery_panel_copy(r,p->cg_textures[2],current.page?0:324,0,108,48,420,340);
    gallery_panel_copy(r,p->cg_textures[2],current.page+1<current.pages?0:324,48,108,48,420,392);
    gallery_panel_copy(r,p->cg_textures[2],0,96,96,52,532,388);
    unsigned percent=total?seen*100/total:0;
    if(percent>=100)gallery_panel_copy(r,p->cg_textures[2],156,148,12,12,76,62);
    if(percent>=10)gallery_panel_copy(r,p->cg_textures[2],144+(int)(percent/10%10)*12,148,12,12,88,62);
    gallery_panel_copy(r,p->cg_textures[2],144+(int)(percent%10)*12,148,12,12,100,62);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,12,18,28,225);SDL_Rect footer={160,674,960,46};SDL_RenderFillRect(r,&footer);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,245,245,245,255);char line[100];snprintf(line,sizeof(line),"Page %u/%u  %s %03u  %s",current.page+1,current.pages,p->selected<images?"CG":"Movie",p->selected<images?p->selected+1:p->selected-images+1,current.tile>=0?"Available":"Locked");panel_text(r,200,678,line);
    return 0;
}

/* 419da0: 15 film thumbnails per page; h04/04 has four additional MOV entries. */
static unsigned gallery_variant_count(MessagePanel *p,KBootstrap *b){
    unsigned count=kgallery_variant_count(&b->gallery,p->back,p->selected);
    return p->back==5&&p->selected==45&&count==4?8:count;
}
static unsigned gallery_variant_page_move(MessagePanel *p,KBootstrap *b,int direction){
    unsigned count=gallery_variant_count(p,b),pages=(count+14)/15;
    if(!pages)return p->variant;
    unsigned page=(p->variant/15+pages+(direction<0?-1:1))%pages,index=p->variant%15,n=count-page*15;
    if(n>15)n=15;
    return page*15+(index<n?index:n-1);
}
static int gallery_variants_draw(MessagePanel *p,KBootstrap *b,SDL_Renderer *r){
    KGalleryThumbnail parent;if(kgallery_thumbnail(&b->gallery,b->vm->bytes,b->vm->byte_count,p->back,p->selected,&parent))return -1;
    unsigned total=gallery_variant_count(p,b),page=p->variant/15,first=page*15,count=total-first;
    if(!total||p->variant>=total)return -1;
    if(count>15)count=15;
    int special=p->back==5&&p->selected==45;
    if(p->cg_cached!=2||p->cg_group!=p->back||p->cg_page!=page||p->cg_count!=p->selected){
        char name[64],suffix[2]={page?(char)('a'+page-1):0,0};
        snprintf(name,sizeof(name),"film_%c%02u_%02u%s.rmt","agefihk"[p->back],parent.page+1,parent.index+1,suffix);
        SDL_Texture *next[3]={gallery_panel_texture(b,r,parent.background,640,480,0),gallery_panel_texture(b,r,name,128,118,2),gallery_panel_texture(b,r,"sp_cg_p.rmt",432,616,1)};
        if(!next[0]||!next[1]||!next[2]){for(unsigned i=0;i<3;i++)SDL_DestroyTexture(next[i]);return -1;}
        gallery_panel_clear(p);memcpy(p->cg_textures,next,sizeof(next));p->cg_cached=2;p->cg_group=p->back;p->cg_page=page;p->cg_count=p->selected;
    }
    gallery_panel_copy(r,p->cg_textures[0],0,0,640,480,0,0);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,0,0,0,128);SDL_Rect dim={160,0,960,720};SDL_RenderFillRect(r,&dim);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    gallery_panel_copy(r,p->cg_textures[2],0,548,288,68,172,404);
    int ox=special?64:count<5?(5-(int)count)*64:0,oy=special?82:count<=5?142:count<=10?82:12;
    int width=0,height=0;SDL_QueryTexture(p->cg_textures[1],NULL,NULL,&width,&height);
    for(unsigned i=0;i<count;i++){
        unsigned base=special?i%4:i;int sx=(int)(base%5)*128,sy=(int)(base/5)*130;
        int x=ox+sx,y=oy+sy+(special&&i>=4?130:0);uint16_t layers[3];
        int unlocked=!kgallery_variant(&b->gallery,p->back,p->selected,first+base,layers)&&kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,layers);
        if(sx+128>width||sy+118>height)return -1;
        /* Copy only border strips for locked entries: no image pixels leak. */
        gallery_panel_copy(r,p->cg_textures[1],sx,sy,128,14,x,y);
        gallery_panel_copy(r,p->cg_textures[1],sx,sy+104,128,14,x,y+104);
        gallery_panel_copy(r,p->cg_textures[1],sx,sy+14,4,90,x,y+14);
        gallery_panel_copy(r,p->cg_textures[1],sx+124,sy+14,4,90,x+124,y+14);
        if(unlocked)gallery_panel_copy(r,p->cg_textures[1],sx+4,sy+14,120,90,x+4,y+14);
        else gallery_panel_copy(r,p->cg_textures[2],144,430,120,90,x+4,y+14);
        if(first+i==p->variant){
            if(unlocked)gallery_panel_copy(r,p->cg_textures[2],144,250,120,90,x+4,y+14);
            SDL_SetRenderDrawColor(r,245,208,80,255);SDL_Rect border={160+(x+4)*3/2-2,(y+14)*3/2-2,184,139};SDL_RenderDrawRect(r,&border);
        }
    }
    gallery_panel_copy(r,p->cg_textures[2],0,page?436:520,48,28,208,424);
    gallery_panel_copy(r,p->cg_textures[2],48,page+1<(total+14)/15?436:520,48,28,256,424);
    gallery_panel_copy(r,p->cg_textures[2],96,520,96,28,328,424);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,12,18,28,225);SDL_Rect footer={160,684,960,36};SDL_RenderFillRect(r,&footer);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,240,240,240,255);char line[100];snprintf(line,sizeof(line),"%s %u/%u",special&&p->variant>=4?"MOV":"CG",p->variant+1,total);panel_text(r,192,696,p->status[0]?p->status:line);
    return 0;
}

/* Navigate the visible thumbnail geometry; page changes belong to L/R. */
static unsigned gallery_grid_move(MessagePanel *p,KBootstrap *b,int action){
    unsigned first,count,current=p->viewing?p->variant:p->selected;int x[15],y[15];
    if(p->viewing){
        unsigned total=gallery_variant_count(p,b);first=current/15*15;count=total-first;if(count>15)count=15;
        unsigned columns=p->back==5&&p->selected==45?4:5;
        for(unsigned i=0;i<count;i++){x[i]=(int)(i%columns)*128;y[i]=(int)(i/columns)*130;}
    }else{
        KGalleryThumbnail t;if(kgallery_thumbnail(&b->gallery,b->vm->bytes,b->vm->byte_count,p->back,current,&t))return current;
        first=t.first;count=t.count;if(count>15)return current;
        for(unsigned i=0;i<count;i++){if(kgallery_thumbnail(&b->gallery,b->vm->bytes,b->vm->byte_count,p->back,first+i,&t))return current;x[i]=(int)t.x;y[i]=(int)t.y;}
    }
    if(!count||current<first||current-first>=count)return current;
    unsigned at=current-first,best=at;int primary=100000,secondary=100000;
    for(unsigned i=0;i<count;i++){
        int dx=x[i]-x[at],dy=y[i]-y[at],along,across;
        if(action==4||action==5){if(dy||(action==4?dx>=0:dx<=0))continue;along=abs(dx);across=0;}
        else {if(action==2?dy>=0:dy<=0)continue;along=abs(dy);across=abs(dx);}
        if(along<primary||(along==primary&&across<secondary)){best=i;primary=along;secondary=across;}
    }
    return first+best;
}

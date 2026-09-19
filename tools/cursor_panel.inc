/* Portable Win32 AND/XOR cursor compositor. No SDL hardware cursor API is
   required; only the 32x32 patch is updated, with the native click hotspot. */
typedef struct {unsigned id,hx,hy;uint8_t pixels[4096];} NativeCursor;
typedef struct {NativeCursor *images;unsigned count,key;uint64_t since,last_stick;SDL_Texture *texture;int x,y,valid,hidden,focused,auto_hidden,meter_x,meter_y;} CursorPanel;
static unsigned cursor_u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static void cursor_clear(CursorPanel *p){
    if(p->hidden)SDL_ShowCursor(SDL_ENABLE);
    SDL_DestroyTexture(p->texture);free(p->images);memset(p,0,sizeof(*p));
}
static int cursor_load(CursorPanel *p,SDL_Renderer *r,const char *path){
    FILE *f=fopen(path,"rb");if(!f)return -1;
    uint8_t header[12];NativeCursor *images=NULL;int bad=0;
    if(fread(header,1,12,f)!=12||memcmp(header,"KACUR01\0",8)||header[8]!=49||header[9]||header[10]||header[11])bad=1;
    if(!bad){images=calloc(49,sizeof(*images));if(!images)bad=1;}
    for(unsigned i=0;!bad&&i<49;i++){
        uint8_t h[8];if(fread(h,1,8,f)!=8){bad=1;break;}
        NativeCursor *c=&images[i];c->id=cursor_u16(h);c->hx=cursor_u16(h+2);c->hy=cursor_u16(h+4);
        if(c->id!=113+i||c->hx>=32||c->hy>=32||h[6]||h[7]||fread(c->pixels,1,4096,f)!=4096){bad=1;break;}
        for(unsigned n=3;n<4096;n+=4)if(c->pixels[n]!=0&&c->pixels[n]!=255){bad=1;break;}
    }
    if(fgetc(f)!=EOF||ferror(f))bad=1;
    fclose(f);if(bad){free(images);return -1;}
    SDL_Texture *texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,32,32);
    if(!texture){free(images);return -1;}
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture,SDL_ScaleModeNearest);
    cursor_clear(p);p->images=images;p->count=49;p->texture=texture;p->focused=1;return 0;
}
/* Both sticks drive the same pointer. Use the stronger stick rather than
   summing them; neutral input never overwrites a USB mouse position. */
static inline void cursor_sticks(CursorPanel *p,KBootstrap *b,int lx,int ly,int rx,int ry){
    int xstick=lx,ystick=ly;
    if((int64_t)rx*rx+(int64_t)ry*ry>(int64_t)lx*lx+(int64_t)ly*ly){xstick=rx;ystick=ry;}
    if(b->bonus52_active==1){
        /* Preserve the original port's rapid stick-motion meter response. */
        if(abs(xstick-p->meter_x)<=1500&&abs(ystick-p->meter_y)<=1500)return;
        p->last_stick=SDL_GetTicks64();p->auto_hidden=0;
        p->meter_x=xstick;p->meter_y=ystick;
        p->x=320+xstick*319/32768;p->y=240-ystick*239/32768;p->valid=1;
        bootstrap_pointer(b,p->x,p->y,0);return;
    }
    p->meter_x=p->meter_y=0;
    int dx=abs(xstick)>6000?xstick/6000:0,dy=abs(ystick)>6000?-ystick/6000:0;
    if(!dx&&!dy)return;
    p->last_stick=SDL_GetTicks64();p->auto_hidden=0;
    int x=p->valid?p->x:320,y=p->valid?p->y:240;
    if(b->area_active&&(b->area_selected>=0||p->valid)){x=b->area_x;y=b->area_y;}
    x+=dx;y+=dy;
    if(x<0)x=0;
    if(x>639)x=639;
    if(y<0)y=0;
    if(y>479)y=479;
    p->x=x;p->y=y;p->valid=1;bootstrap_pointer(b,x,y,0);
}
static unsigned cursor_shape(const KBootstrap *b){
    /* 40ff98: AREA high byte chooses paired resource 115 + 2*kind.
       4102fa: tool mode uses the fixed native 4fd8b8 table instead. */
    if(b->area_active==2){
        static const unsigned tools[]={115,147,121,149,151,133,153,155,157,159,141};
        unsigned at=b->vm->bytes[1950],count=b->vm->bytes[1951];
        if(at<count){unsigned tool=b->vm->bytes[1952+at];if(tool<sizeof(tools)/sizeof(*tools))return tools[tool];}
    }else if(b->area_active&&b->area_selected>=0&&(unsigned)b->area_selected<b->area_count){
        unsigned kind=((unsigned)b->areas[b->area_selected][0]>>8)&255;
        if(kind<23)return 115+kind*2;
    }
    return 113;
}
static void cursor_pixels(const NativeCursor *c,const KBootstrap *b,int x,int y,uint8_t out[4096]){
    memset(out,0,4096);
    const KImage *im=&b->layers[0];if(!im->pixels)return;
    for(int cy=0;cy<32;cy++)for(int cx=0;cx<32;cx++){
        int sx=x-(int)c->hx+cx,sy=y-(int)c->hy+cy;
        if(sx<0||sy<0||sx>=640||sy>=480)continue;
        const uint8_t *mask=c->pixels+(cy*32+cx)*4,*src=im->pixels+sy*im->stride+sx*4;
        uint8_t *dst=out+(cy*32+cx)*4;
        /* Preserve transparent pixels, and apply true XOR for every palette
           color, including the native monochrome invert operation. */
        if(mask[3]==255&&!mask[0]&&!mask[1]&&!mask[2])continue;
        for(unsigned k=0;k<3;k++){
            unsigned background=src[k];
            if(b->fade_visible&&b->fade_surface.pixels){unsigned a=b->fade_alpha;background=(background*(255-a)+b->fade_surface.pixels[sy*b->fade_surface.stride+sx*4+k]*a)/255;}
            dst[k]=(uint8_t)((background&mask[3])^mask[k]);
        }
        dst[3]=255;
    }
}
static int cursor_draw(CursorPanel *p,KBootstrap *b,SDL_Renderer *r,int covered,uint64_t now){
    if(p->last_stick&&now-p->last_stick>=5000)p->auto_hidden=1;
    int area=b->area_active&&b->area_selected>=0;
    int show=p->texture&&p->focused&&!covered&&!p->auto_hidden&&(area||p->valid);
    int x=area?b->area_x:p->x,y=area?b->area_y:p->y;
    show=show&&x>=0&&x<640&&y>=0&&y<480;
    /* Keep the platform pointer enabled on menu panels as a reliable
       fallback; the software cursor remains the game-accurate overlay. */
    if(show!=p->hidden){SDL_ShowCursor(show?SDL_ENABLE:SDL_DISABLE);p->hidden=show;}
    if(!show)return 0;
    unsigned key=cursor_shape(b);if(key!=p->key){p->key=key;p->since=now;}
    unsigned id=key+(key!=113?(unsigned)((now-p->since)/200)%2:0);
    if(id<113||id-113>=p->count)return -1;
    NativeCursor *c=&p->images[id-113];uint8_t pixels[4096];cursor_pixels(c,b,x,y,pixels);
    if(SDL_UpdateTexture(p->texture,NULL,pixels,128))return -1;
    SDL_Rect old;SDL_RenderGetClipRect(r,&old);SDL_bool clipped=SDL_RenderIsClipEnabled(r);
    SDL_Rect clip={160,0,960,720};SDL_RenderSetClipRect(r,&clip);
    SDL_FRect dst={160+(x-(int)c->hx)*1.5f,(y-(int)c->hy)*1.5f,48,48};
    int result=SDL_RenderCopyF(r,p->texture,NULL,&dst);SDL_RenderSetClipRect(r,clipped?&old:NULL);return result;
}

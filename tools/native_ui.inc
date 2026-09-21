/* Shared software composition for original 640x480 menu artwork. */
#include "akb.h"
static inline int ui_canvas(KImage *p){
    if(p->pixels&&p->width==640&&p->height==480)return 0;
    rmt_free(p);*p=(KImage){0,0,640,480,2560,calloc(640*480,4)};return p->pixels?0:-1;
}
static inline int ui_asset(KBootstrap *b,const char *name,KImage *out){
    if(out->pixels)return 0;
    /* ai6_read_named honours loose override files, so translated artwork that
       ships outside rmt.arc is used by the UI panels too. */
    uint8_t *data=NULL;size_t size=0;
    int bad=ai6_read_named(&b->images,name,&data,&size)||rmt_decode(data,size,out);free(data);return bad?-1:0;
}
static inline int ui_akb_asset(KBootstrap *b,const char *name,KImage *out){
    if(out->pixels)return 0;
    uint8_t *data=NULL;size_t size=0;
    int bad=ai6_read_named(&b->images,name,&data,&size)||akb_decode(data,size,out);free(data);return bad?-1:0;
}
static inline void ui_fill(KImage *p,int x,int y,int w,int h,unsigned rgb){
    for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)if(i>=0&&j>=0&&(unsigned)i<p->width&&(unsigned)j<p->height){
        uint8_t *d=p->pixels+j*p->stride+i*4;d[0]=(uint8_t)rgb;d[1]=(uint8_t)(rgb>>8);d[2]=(uint8_t)(rgb>>16);d[3]=255;
    }
}
static inline void ui_blit(KImage *d,const KImage *s,int sx,int sy,int w,int h,int dx,int dy,int alpha){
    if(!s->pixels)return;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        if(sx+x<0||sy+y<0||dx+x<0||dy+y<0||(unsigned)(sx+x)>=s->width||(unsigned)(sy+y)>=s->height||(unsigned)(dx+x)>=d->width||(unsigned)(dy+y)>=d->height)continue;
        const uint8_t *p=s->pixels+(sy+y)*s->stride+(sx+x)*4;uint8_t *q=d->pixels+(dy+y)*d->stride+(dx+x)*4;
        if(alpha==2&&p[0]==0&&p[1]==255&&p[2]==0)continue;
        unsigned a=alpha==1?p[3]:255;for(unsigned c=0;c<3;c++)q[c]=(uint8_t)(p[c]*a/255+q[c]*(255-a)/255);q[3]=255;
    }
}
static inline void ui_scale(KImage *d,const KImage *s,int dx,int dy,unsigned w,unsigned h){
    if(!s->pixels||!w||!h)return;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)if(dx+(int)x>=0&&dy+(int)y>=0&&(unsigned)dx+x<d->width&&(unsigned)dy+y<d->height){
        uint8_t *out=d->pixels+((unsigned)dy+y)*d->stride+((unsigned)dx+x)*4;
        const uint8_t *in=s->pixels+(y*s->height/h)*s->stride+(x*s->width/w)*4;memcpy(out,in,3);out[3]=255;
    }
}
/* The panel font follows the runtime's effective encoding, so a translated
   script pack gets the Simplified glyph set instead of a stale CP932 face. */
static inline KFont *ui_font(KBootstrap *b){
    return bootstrap_ui_font(b);
}
static inline int ui_text_scroll(KBootstrap *b,KImage *p,const char *text,int x,int y,int width,int height,unsigned color,int encoded,unsigned skip){
    KTextChar chars[4096];size_t n=0;if(!ui_font(b))return -1;
    if(encoded){
        /* Same auto-detection and sticky GBK state as the story text: the
           backlog stores raw script bytes, so decoding them with the configured
           encoding alone produced mojibake under a translation pack. */
        if(bootstrap_decode_ui_text(b,text,strlen(text),chars,4096,&n))return -1;
    }else{const unsigned char *s=(const unsigned char *)text;while(*s&&n<4096){unsigned c=*s++,extra=c<128?0:(c&224)==192?1:(c&240)==224?2:3;c&=extra?(1u<<(6-extra))-1:127;
        for(unsigned i=0;i<extra;i++){if((*s&192)!=128)return -1;c=(c<<6)|(*s++&63);}chars[n++]=(KTextChar){c,c<128?1:2};}}
    int xx=x;unsigned line=0;for(size_t i=0;i<n;i++){
        unsigned c=chars[i].codepoint;if(c=='\r')continue;
        if(c=='\n'){xx=x;line++;continue;}int advance=(int)chars[i].columns*8;
        if(xx+advance>x+width){xx=x;line++;}
        if(line>=skip){int yy=y+(int)(line-skip)*20;if(yy+16<=y+height&&kfont_draw(b->font,p,c,xx,yy,16,16,color))return -1;}xx+=advance;
    }return (int)line+1;
}
static inline int ui_text(KBootstrap *b,KImage *p,const char *text,int x,int y,int width,int height,unsigned color,int encoded){
    return ui_text_scroll(b,p,text,x,y,width,height,color,encoded,0)<0?-1:0;
}

static inline uint64_t ui_hash(uint64_t hash,const void *data,size_t size){
    const uint8_t *p=data;for(size_t i=0;i<size;i++)hash=(hash^p[i])*1099511628211ULL;return hash;
}
static inline void ui_present(SDL_Renderer *r,KImage *image,SDL_Texture **texture){
    if(*texture){int w,h;SDL_QueryTexture(*texture,NULL,NULL,&w,&h);if(w!=640||h!=480){SDL_DestroyTexture(*texture);*texture=NULL;}}
    if(!*texture)*texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,640,480);
    if(*texture){SDL_UpdateTexture(*texture,NULL,image->pixels,(int)image->stride);SDL_Rect dst={160,0,960,720};SDL_RenderCopy(r,*texture,NULL,&dst);}
}

static inline void ui_utf8_note(const char *encoded,char out[129]){
    KTextChar chars[42];size_t count=0,n=0;out[0]=0;
    if(ktext_decode(KTEXT_CP932,(const uint8_t *)encoded,strlen(encoded),chars,42,&count))return;
    for(size_t i=0;i<count;i++){uint32_t c=chars[i].codepoint;
        if(c<128)out[n++]=(char)c;
        else if(c<2048){out[n++]=(char)(0xc0|(c>>6));out[n++]=(char)(0x80|(c&63));}
        else{out[n++]=(char)(0xe0|(c>>12));out[n++]=(char)(0x80|((c>>6)&63));out[n++]=(char)(0x80|(c&63));}
    }out[n]=0;
}
static inline void ui_edit_text(char *text,size_t capacity,const SDL_Event *e){
    if(e->type==SDL_TEXTINPUT){size_t n=strlen(text),add=strlen(e->text.text);if(n+add<capacity)memcpy(text+n,e->text.text,add+1);}
    if(e->type==SDL_KEYDOWN&&(e->key.keysym.sym==SDLK_BACKSPACE||e->key.keysym.sym==SDLK_DELETE)){size_t n=strlen(text);if(n){n--;while(n&&((unsigned char)text[n]&0xc0)==0x80)n--;text[n]=0;}}
}

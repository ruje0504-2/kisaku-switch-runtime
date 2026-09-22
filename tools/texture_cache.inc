/* Exact row comparison: unchanged textures incur no driver upload. This is
   presentation-only storage, independent of script-owned surfaces. */
typedef struct {SDL_Texture *texture;KImage copy;uint64_t uploads,bytes;} KTextureCache;
static void texture_cache_clear(KTextureCache *c){SDL_DestroyTexture(c->texture);rmt_free(&c->copy);memset(c,0,sizeof(*c));}
static int texture_cache_update(KTextureCache *c,SDL_Renderer *r,const KImage *im,SDL_BlendMode blend){
    if(!im||!im->pixels||!im->width||!im->height||im->stride<(size_t)im->width*4)return -1;
    int fresh=!c->texture||c->copy.width!=im->width||c->copy.height!=im->height;
    if(fresh){
        SDL_Texture *t=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,(int)im->width,(int)im->height);
        uint8_t *pixels=malloc((size_t)im->width*im->height*4);
        if(!t||!pixels||SDL_SetTextureBlendMode(t,blend)){SDL_DestroyTexture(t);free(pixels);return -1;}
        texture_cache_clear(c);c->texture=t;c->copy=(KImage){0,0,im->width,im->height,(size_t)im->width*4,pixels};
    }
    unsigned first=im->height,last=0;size_t row=(size_t)im->width*4;
    for(unsigned y=0;y<im->height;y++)if(fresh||memcmp(c->copy.pixels+y*row,im->pixels+y*im->stride,row)){
        if(first==im->height)first=y;
        last=y+1;
    }
    if(first==im->height)return 0;
    SDL_Rect rect={0,(int)first,(int)im->width,(int)(last-first)};
    if(SDL_UpdateTexture(c->texture,&rect,im->pixels+first*im->stride,(int)im->stride)){
        /* An upload can partially fail. Force a full replacement next time. */
        texture_cache_clear(c);return -1;
    }
    for(unsigned y=first;y<last;y++)memcpy(c->copy.pixels+y*row,im->pixels+y*im->stride,row);
    c->uploads++;c->bytes+=(last-first)*row;return 0;
}

#include "image_sdl.h"
#include <limits.h>
SDL_Texture *kimage_texture(SDL_Renderer *r,const KImage *im) {
    if(!r||!im||!im->pixels||!im->width||!im->height||im->width>INT_MAX||im->height>INT_MAX||im->stride>INT_MAX){SDL_SetError("Invalid KImage");return NULL;}
    SDL_Texture *t=SDL_CreateTexture(r,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STATIC,(int)im->width,(int)im->height);
    if(!t)return NULL;
    /* The game canvas and AKB artwork are authored in integer pixels.  The
       viewer presents the 640x480 canvas at 1.5x, so linear filtering would
       blend every edge and make the supplied font look fuzzy. */
    if(SDL_UpdateTexture(t,NULL,im->pixels,(int)im->stride)||SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND)||SDL_SetTextureScaleMode(t,SDL_ScaleModeNearest)){SDL_DestroyTexture(t);return NULL;}
    return t;
}
SDL_Rect kimage_fit(uint32_t w,uint32_t h,SDL_Rect area) {
    if(!w||!h||area.w<=0||area.h<=0)return (SDL_Rect){0,0,0,0};
    double scale=(double)area.w/w,sy=(double)area.h/h;if(sy<scale)scale=sy;
    int rw=(int)(w*scale),rh=(int)(h*scale);if(rw<1)rw=1;if(rh<1)rh=1;
    return (SDL_Rect){area.x+(area.w-rw)/2,area.y+(area.h-rh)/2,rw,rh};
}

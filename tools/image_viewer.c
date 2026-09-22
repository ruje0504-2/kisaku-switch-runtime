/* Resource inspection frontend, deliberately separate from the game VM. */
#include "ai6arc.h"
#include "rmt.h"
#include "image_sdl.h"
#include <SDL_test_font.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
static int same(const char *a,const char *b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return *a==*b;}
static SDL_Texture *load(SDL_Renderer *r,Ai6Archive *a,uint32_t i,KImage *metadata) {
    uint8_t *data=NULL;size_t size=0;KImage im={0};
    if(ai6_read(a,i,&data,&size)||rmt_decode(data,size,&im)){free(data);SDL_SetError("Cannot decode %s",a->entries[i].name);return NULL;}
    free(data);SDL_Texture *t=kimage_texture(r,&im);*metadata=im;metadata->pixels=NULL;rmt_free(&im);return t;
}
static int capture(SDL_Renderer *r,const char *path) {
    int w,h;if(SDL_GetRendererOutputSize(r,&w,&h))return -1;
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_BGRA32);if(!s)return -1;
    int result=SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_BGRA32,s->pixels,s->pitch);
    if(!result)result=SDL_SaveBMP(s,path);
    SDL_FreeSurface(s);
    return result;
}
int main(int argc,char **argv) {
    const char *path=NULL,*name="kisaku_dl_title_p.akb",*shot=NULL;int frames=0;
#ifdef __SWITCH__
    path="sdmc:/switch/kisaku/game/layer.arc";
    (void)argc;(void)argv;
#else
    if(argc<2){fprintf(stderr,"Usage: %s layer.arc [--name resource.akb] [--frames N] [--screenshot out.bmp]\n",argv[0]);return 2;}
    path=argv[1];
    for(int i=2;i<argc;i++) {
        if(i+1>=argc)return 2;
        if(!strcmp(argv[i],"--name"))name=argv[++i];
        else if(!strcmp(argv[i],"--screenshot"))shot=argv[++i];
        else if(!strcmp(argv[i],"--frames")){char *end;long n=strtol(argv[++i],&end,10);if(*end||n<1||n>100000)return 2;frames=(int)n;}
        else return 2;
    }
#endif
    int result=1;Ai6Archive arc={0};SDL_Window *window=NULL;SDL_Renderer *r=NULL;SDL_Texture *texture=NULL;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    window=SDL_CreateWindow("KISAKU AKB resource viewer - not gameplay",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);
    if(!window)goto done;
    r=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!r)r=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!r||SDL_RenderSetLogicalSize(r,1280,720))goto done;
    char problem[256]="";
    uint32_t selected=0;KImage im={0};
    if(ai6_open(&arc,path)||!arc.count)snprintf(problem,sizeof(problem),"Cannot open layer.arc. Copy it to switch/kisaku/ELFIMAGE/ on SD.");
    else {
        unsigned found=0;for(uint32_t i=0;i<arc.count;i++)if(same(arc.entries[i].name,name)){selected=i;found=1;break;}
        if(!found)snprintf(problem,sizeof(problem),"Resource not found: %.180s",name);
        else {texture=load(r,&arc,selected,&im);if(!texture)snprintf(problem,sizeof(problem),"%.240s",SDL_GetError());}
    }
#ifdef __SWITCH__
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
#endif
    int running=1,alpha=1,ticks=0;
    while(running) {
#ifdef __SWITCH__
        if(!appletMainLoop())break;
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);
        int move=(down&HidNpadButton_Right)?1:(down&HidNpadButton_Left)?-1:0;
        if(down&HidNpadButton_X)alpha=!alpha;
#else
        int move=0;
#endif
        SDL_Event e;while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT)running=0;
            if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
                switch(e.key.keysym.sym){case SDLK_ESCAPE:running=0;break;case SDLK_LEFT:move=-1;break;case SDLK_RIGHT:move=1;break;case SDLK_x:alpha=!alpha;break;default:break;}
            }
            if(e.type==SDL_FINGERUP&&e.tfinger.y>0.89f)move=e.tfinger.x<0.5f?-1:1;
            if(e.type==SDL_MOUSEBUTTONUP&&e.button.y>640)move=e.button.x<640?-1:1;
        }
        if(move&&arc.count) {
            selected=(uint32_t)(((int64_t)selected+move+arc.count)%arc.count);
            SDL_DestroyTexture(texture);texture=load(r,&arc,selected,&im);
            problem[0]=0;if(!texture)snprintf(problem,sizeof(problem),"%.240s",SDL_GetError());
        }
        SDL_SetRenderDrawColor(r,15,18,24,255);SDL_RenderClear(r);
        SDL_SetRenderDrawColor(r,220,225,235,255);
        SDLTest_DrawString(r,24,22,"KISAKU / AI6WIN - AKB RESOURCE VIEWER (NOT GAMEPLAY)");
        if(texture) {
            SDL_Rect dst=kimage_fit(im.width,im.height,(SDL_Rect){20,60,1240,560});
            for(int y=dst.y;y<dst.y+dst.h;y+=20)for(int x=dst.x;x<dst.x+dst.w;x+=20){
                int c=((x-dst.x)/20+(y-dst.y)/20)%2?75:45;
                SDL_SetRenderDrawColor(r,c,c,c,255);SDL_Rect tile={x,y,20,20};
                if(tile.x+tile.w>dst.x+dst.w)tile.w=dst.x+dst.w-tile.x;
                if(tile.y+tile.h>dst.y+dst.h)tile.h=dst.y+dst.h-tile.y;
                SDL_RenderFillRect(r,&tile);
            }
            SDL_SetTextureBlendMode(texture,alpha?SDL_BLENDMODE_BLEND:SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(texture,SDL_ScaleModeNearest);
            if(SDL_RenderCopy(r,texture,NULL,&dst))goto done;
            char info[512];snprintf(info,sizeof(info),"%u/%u  %.260s  %ux%u  origin=(%d,%d)  alpha=%s",selected+1,arc.count,arc.entries[selected].name,im.width,im.height,im.x,im.y,alpha?"on":"off");
            SDL_SetRenderDrawColor(r,220,225,235,255);SDLTest_DrawString(r,24,634,info);
        } else {SDL_SetRenderDrawColor(r,255,130,110,255);SDLTest_DrawString(r,24,300,problem);}
        SDL_SetRenderDrawColor(r,220,225,235,255);
        SDLTest_DrawString(r,24,672,"LEFT / RIGHT: browse   X: alpha   HOME: system menu   Touch bottom left/right: browse");
        ++ticks;
        if(shot&&((frames&&ticks==frames)||(!frames&&ticks==1))&&capture(r,shot))goto done;
        SDL_RenderPresent(r);
        if(frames&&ticks>=frames)break;
        SDL_Delay(8);
    }
    result=problem[0]?1:0;
done:
    if(result)fprintf(stderr,"Viewer failed: %s\n",SDL_GetError());
    SDLTest_CleanupTextDrawing();SDL_DestroyTexture(texture);SDL_DestroyRenderer(r);SDL_DestroyWindow(window);ai6_close(&arc);SDL_Quit();return result;
}

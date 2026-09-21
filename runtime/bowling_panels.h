#ifndef KISAKU_BOWLING_PANELS_H
#define KISAKU_BOWLING_PANELS_H
#include "bowling_draw.h"
/* 48e820 -> 48c770: green becomes transparent, red becomes a supplied
   black shadow (0x80000000 for actors, 0x40000000 for tutorial). */
static inline void kbowling_draw_shadow(KImage *dst,int dx,int dy,const KImage *src,int sx,int sy,int w,int h,unsigned shadow){
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        int xx=dx+x,yy=dy+y;if(xx<0||yy<0||xx>=(int)dst->width||yy>=(int)dst->height)continue;
        const uint8_t *p=src->pixels+(size_t)(sy+y)*src->stride+(size_t)(sx+x)*4;
        if(!p[0]&&p[1]==255&&!p[2])continue;
        uint8_t *q=dst->pixels+(size_t)yy*dst->stride+(size_t)xx*4;
        unsigned alpha=p[3],red=!p[0]&&!p[1]&&p[2]==255;
        if(red)alpha=shadow;
        for(unsigned c=0;c<3;c++)q[c]=(uint8_t)((red?0:p[c]*alpha/255)+q[c]*(255-alpha)/255);
    }
}
static inline int kbowling_draw_actor(KImage *dst,const KImage *atlas,KBowlingVector position,int sprite,unsigned cpu){
    if(sprite<0)return 0;
    unsigned w=cpu?192:144,h=cpu?208:180,col=(unsigned)sprite%8,row=(unsigned)sprite/8;
    if(!cpu&&sprite>=17&&sprite<=19){static const unsigned map[3]={3,1,2};col=map[sprite-17];}
    if(!kbowling_image_fits(dst,640,480)||!kbowling_image_fits(atlas,(col+1)*w,(row+1)*h))return -1;
    KBowlingCamera camera=kbowling_camera();int x,y;if(kbowling_project(&camera,position,0,&x,&y))return -1;
    x+=(cpu?104:128)-188;y+=-10-116;
    kbowling_draw_shadow(dst,x,y,atlas,(int)(col*w),(int)(row*h),(int)w,(int)h,128);return 0;
}
static inline int kbowling_draw_tutorial(KImage *dst,const KImage *atlas,unsigned page,int focus,unsigned pressed){
    if(page>1||!kbowling_image_fits(dst,640,480)||!kbowling_image_fits(atlas,542,900))return -1;
    /* 45d650 creates a full-screen black surface with Alpha128. */
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *p=dst->pixels+y*dst->stride+x*4;for(unsigned c=0;c<3;c++)p[c]=(uint8_t)(p[c]*127/255);
    }
    kbowling_draw_shadow(dst,49,33,atlas,0,(int)page*414,542,414,64);
    int state=focus==0?(pressed?2:1):0;
    kbowling_draw_rect(dst,49+215,33+375,atlas,88,828+state*24,112,24,0);
    if(page){state=focus==256?(pressed?2:1):0;kbowling_draw_rect(dst,49+63,33+375,atlas,0,828+state*24,88,24,0);}
    else{state=focus==257?(pressed?2:1):0;kbowling_draw_rect(dst,49+391,33+375,atlas,200,828+state*24,88,24,0);}
    return 0;
}
static inline int kbowling_tutorial_hit(unsigned page,int x,int y){
    x-=49;y-=33;if(y<375||y>=399)return -1;
    if(x>=215&&x<327)return 0;
    if(page&&x>=63&&x<151)return 256;
    if(!page&&x>=391&&x<479)return 257;
    return -1;
}
static inline int kbowling_draw_guide(KImage *dst,const KImage *parts,const KBowlingWorld *world,double speed){
    if(!world||!isfinite(speed)||!kbowling_image_fits(dst,640,480)||!kbowling_image_fits(parts,640,344))return -1;
    /* 54504c / 4d0cc0 native pin-state tile coordinates. */
    static const unsigned xy[10][2]={{103,55},{79,44},{127,44},{55,32},{103,32},{151,32},{31,20},{79,20},{127,20},{175,20}};
    for(unsigned i=0;i<10;i++)kbowling_draw_rect(dst,386+(int)xy[i][0],126+(int)xy[i][1],parts,
        world->present[i]&&!world->fallen[i]?298:272,25+(int)i*14,26,14,1);
    /* 4d0510 truncates m/s * 60 * 60 / 1000 to km/h, clamps 10..40. */
    int km=speed>=40.0/3.6?40:speed<=10.0/3.6?10:(int)(speed*60*60/1000);
    int empty=200-(km-10)*200/30;
    kbowling_draw_rect(dst,599,232,parts,213,106,24,200,1);
    kbowling_draw_rect(dst,599,232+empty,parts,248,12+empty,24,200-empty,1);
    if(speed>0){
        kbowling_draw_rect(dst,597,204,parts,272+(km/10)*8,0,8,14,1);
        kbowling_draw_rect(dst,605,204,parts,272+(km%10)*8,0,8,14,1);
    }
    return 0;
}
/* 4ced00 / 4d6400 use the compact five-row atlas, not the live two-row
   score skin. Final ranks are competition ranks: equal scores share rank. */
static inline int kbowling_draw_results(KImage *dst,const KImage *atlas,const KBowlingMatch *match,unsigned final){
    if(!match||!kbowling_image_fits(dst,640,480)||!kbowling_image_fits(atlas,492,432))return -1;
    unsigned order[5]={0,1,2,3,4};
    for(unsigned i=0;i<4;i++)if(match->characters[i]>8)return -1;
    if(final){
        for(unsigned i=0;i<5;i++)if(match->scores[i].total==UINT16_MAX)return -1;
        for(unsigned i=1;i<5;i++){
            unsigned id=order[i],j=i;while(j&&match->scores[order[j-1]].total<match->scores[id].total){order[j]=order[j-1];j--;}order[j]=id;
        }
    }
    kbowling_draw_shadow(dst,74,100,atlas,0,0,492,270,128);
    if(final)kbowling_draw_rect(dst,74,100,atlas,0,270,488,24,0);
    unsigned rank=0;
    for(unsigned row=0;row<5;row++){
        unsigned player=order[row],role=player?match->characters[player-1]:9;
        const KBowlingScore *score=&match->scores[player];
        if(final){
            if(row&&score->total<match->scores[order[row-1]].total)rank=row;
            kbowling_draw_rect(dst,82,145+(int)row*44,atlas,(int)rank*80,380,80,20,0);
        }
        kbowling_draw_rect(dst,82,165+(int)row*44,atlas,(int)(role%5)*88,400+(int)(role/5)*16,88,16,0);
        for(unsigned frame=0;frame<10;frame++){
            unsigned a=kbowling_score_pins(score,frame,0);if(a==15)break;
            kbowling_draw_rect(dst,174+(int)frame*36,144+(int)row*44,atlas,frame<9?0:38,294,frame<9?38:56,38,0);
            for(unsigned ball=0;ball<(frame<9?2u:3u);ball++){
                unsigned pins=kbowling_score_pins(score,frame,ball);if(pins==15||(frame<9&&ball==1&&a==10))break;
                unsigned glyph=kbowling_score_glyph(score,frame,ball);
                /* Split glyphs are baked into the compact atlas. */
                if((ball==0||(frame==9&&ball==1&&a==10)||(frame==9&&ball==2&&(a!=10||kbowling_score_pins(score,frame,1)==10)))&&
                   !(score->rolls[frame][ball]&32)&&(score->rolls[frame][ball]&16))glyph=pins+10;
                kbowling_draw_rect(dst,176+(int)frame*36+(int)ball*18,146+(int)row*44,atlas,(int)(glyph%13)*16,332+(int)(glyph/13)*16,16,16,0);
            }
        }
        unsigned sum=0;
        for(unsigned frame=0;frame<10&&score->frames[frame]!=UINT16_MAX;frame++){
            sum+=score->frames[frame];int x=74+(frame<9?104+(int)frame*36:437),y=164+(int)row*44;
            unsigned hundreds=sum/100%10,tens=sum/10%10;
            if(hundreds)kbowling_draw_rect(dst,x,y,atlas,(int)hundreds*10,364,10,16,0);
            if(hundreds||tens)kbowling_draw_rect(dst,x+10,y,atlas,(int)tens*10,364,10,16,0);
            kbowling_draw_rect(dst,x+20,y,atlas,(int)(sum%10)*10,364,10,16,0);
        }
    }
    return 0;
}
#endif

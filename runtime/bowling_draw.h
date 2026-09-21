#ifndef KISAKU_BOWLING_DRAW_H
#define KISAKU_BOWLING_DRAW_H
#include "bowling_projection.h"
#include "bowling_session.h"
#include "rmt.h"
/* Sources remain borrowed from the MES layers. Rendering is independent of
   the VM so failed resource validation cannot consume a native call. */
typedef struct {const KImage *background,*pins,*ball,*parts;} KBowlingArt;
static inline int kbowling_image_fits(const KImage *im,unsigned w,unsigned h){
    return im&&im->pixels&&im->width>=w&&im->height>=h&&im->stride>=(size_t)im->width*4;
}
static inline void kbowling_draw_rect(KImage *dst,int dx,int dy,const KImage *src,int sx,int sy,int w,int h,int key){
    for(int y=0;y<h;y++){
        int yy=dy+y,source_y=sy+y;if(yy<0||yy>=(int)dst->height||source_y<0||source_y>=(int)src->height)continue;
        for(int x=0;x<w;x++){
            int xx=dx+x,source_x=sx+x;if(xx<0||xx>=(int)dst->width||source_x<0||source_x>=(int)src->width)continue;
            const uint8_t *p=src->pixels+(size_t)source_y*src->stride+(size_t)source_x*4;
            if(key&&!p[0]&&p[1]==255&&!p[2])continue;
            uint8_t *q=dst->pixels+(size_t)yy*dst->stride+(size_t)xx*4;
            memcpy(q,p,3);q[3]=255;
        }
    }
}
static inline void kbowling_draw_number(KImage *dst,const KImage *parts,unsigned number,int x,int y,int total){
    unsigned hundreds=number/100%10,tens=number/10%10,units=number%10;int base=total?380:0;
    if(hundreds)kbowling_draw_rect(dst,x,y,parts,base+(int)hundreds*10,460,10,20,0);
    if(hundreds||tens)kbowling_draw_rect(dst,x+10,y,parts,base+(int)tens*10,460,10,20,0);
    kbowling_draw_rect(dst,x+20,y,parts,base+(int)units*10,460,10,20,0);
}
static inline void kbowling_draw_score(KImage *dst,const KImage *parts,const KBowlingMatch *match){
    kbowling_draw_rect(dst,4,4,parts,0,344,634,116,1);
    if(match->mode){
        kbowling_draw_rect(dst,18,62,parts,0,480,606,44,0);
        kbowling_draw_rect(dst,19,79,parts,324,25+(int)match->characters[0]*26,70,26,0);
    }
    for(unsigned player=0;player<(match->mode?2u:1u);player++){
        const KBowlingScore *score=&match->scores[player];unsigned sum=0,resolved=1;
        for(unsigned frame=0;frame<10;frame++){
            for(unsigned ball=0;ball<(frame==9?3u:2u);ball++){
                unsigned pins=kbowling_score_pins(score,frame,ball);
                if(pins==15||(frame<9&&ball==1&&kbowling_score_pins(score,frame,0)==10))continue;
                unsigned glyph=kbowling_score_glyph(score,frame,ball);if(glyph>12)continue;
                int x=4+87+(int)frame*44+(int)ball*22,y=4+15+(int)player*44;
                kbowling_draw_rect(dst,x,y,parts,100+(int)glyph*20,460,20,20,1);
                if(score->rolls[frame][ball]&16)kbowling_draw_rect(dst,x,y,parts,360,460,20,20,1);
            }
            if(score->frames[frame]==UINT16_MAX)resolved=0;
            if(resolved){sum+=score->frames[frame];kbowling_draw_number(dst,parts,sum,4+(frame<9?93+(int)frame*44:500),4+37+(int)player*44,0);}
        }
        if(score->total!=UINT16_MAX)kbowling_draw_number(dst,parts,score->total,4+568,4+37+(int)player*44,1);
    }
}
/* Frontend supplies poses selected at the first collision (4d1170) and
   optional raised rack coordinates during 4d3490/4d3900 transitions. */
static inline int kbowling_draw(KImage *dst,KBowlingArt art,const KBowlingSession *s,
    const unsigned pose[10],const KBowlingVector *rack_positions,unsigned show_ball,unsigned show_guide){
    if(!s||!pose||!kbowling_image_fits(dst,640,480)||!kbowling_image_fits(art.background,640,480)||
        !kbowling_image_fits(art.pins,482,288)||!kbowling_image_fits(art.ball,640,128)||!kbowling_image_fits(art.parts,640,524))return -1;
    KBowlingCamera camera=kbowling_camera();
    KBowlingVector positions[11];unsigned order[11],count=0;
    for(unsigned i=0;i<10;i++)if(s->world.present[i]){order[count++]=i;positions[i]=rack_positions?rack_positions[i]:kbowling_pin_position(&s->world.pins[i]);}
    if(show_ball){order[count++]=10;positions[10]=s->world.ball;}
    int px[11],py[11];
    for(unsigned i=0;i<count;i++)if(kbowling_project(&camera,positions[order[i]],order[i]==10,&px[order[i]],&py[order[i]]))return -1;
    /* Native depth order is far to near; stable ties retain insertion order. */
    for(unsigned i=1;i<count;i++){unsigned id=order[i],j=i;while(j&&positions[order[j-1]].z<positions[id].z){order[j]=order[j-1];j--;}order[j]=id;}
    kbowling_draw_rect(dst,0,0,art.background,0,0,640,480,0);
    for(unsigned i=0;i<count;i++){
        unsigned id=order[i];int sx,sy;
        if(id==10){kbowling_ball_tile(positions[id].z,&sx,&sy);kbowling_draw_rect(dst,px[id],py[id],art.ball,sx,sy,32,32,1);}
        else{kbowling_pin_tile(positions[id].z,pose[id],s->world.pose_elapsed[id],&sx,&sy);kbowling_draw_rect(dst,px[id],py[id],art.pins,sx,sy,24,24,1);}
    }
    /* 4d5390 keeps the lane lip at layer13, over the depth-sorted objects. */
    kbowling_draw_rect(dst,0,120,art.pins,96,0,386,53,0);
    if(show_guide)kbowling_draw_rect(dst,386,126,art.parts,0,0,248,344,1);
    kbowling_draw_score(dst,art.parts,&s->match);return 0;
}
#endif

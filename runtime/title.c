#include "title.h"
#include <string.h>
static int blend(KImage *dst,const KImage *src,int dx,int dy,unsigned sx,unsigned sy,unsigned w,unsigned h){
    if(dx<0||dy<0||(uint64_t)(unsigned)dx+w>dst->width||(uint64_t)(unsigned)dy+h>dst->height||(uint64_t)sx+w>src->width||(uint64_t)sy+h>src->height)return -1;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        const uint8_t *p=src->pixels+(sy+y)*src->stride+(sx+x)*4;
        uint8_t *q=dst->pixels+((unsigned)dy+y)*dst->stride+((unsigned)dx+x)*4;
        /* 43f650 uses two independently truncated /255 table entries. */
        for(unsigned c=0;c<3;c++)q[c]=(uint8_t)((q[c]*(255-p[3]))/255+(p[c]*p[3])/255);
    }return 0;
}
int ktitle_draw(const KTitle *t,KImage *screen){
    if(!t||!screen||!screen->pixels||screen->width!=640||screen->height!=480||t->background.width<640||t->background.height<480||!t->background.pixels||!t->parts.pixels)return -1;
    for(unsigned y=0;y<480;y++)memcpy(screen->pixels+y*screen->stride,t->background.pixels+y*t->background.stride,640*4);
    if(t->variant==4){
        if(t->count<5||t->count>6)return -1;
        for(unsigned i=0;i<t->count;i++){
            int id=t->native_ids[i];unsigned source=id<0?(i==1?1:3):(unsigned)id;
            unsigned sy=(source/3)*128+(id<0?96:t->selected==(int)i?32:0);
            if(blend(screen,&t->parts,452,260+(int)i*36,(source%3)*188,sy,188,32))return -1;
        }
        return 0;
    }
    if(t->variant==3){
        int y=280;static const unsigned sy[]={0,108,216,360},h[]={36,36,48,36};
        for(unsigned i=0;i<t->count;i++){unsigned id=t->extra_ids[i];if(id>3)return -1;if(blend(screen,&t->parts,176,y,0,sy[id]+(t->selected==(int)i?h[id]:0),288,h[id]))return -1;y+=(int)h[id]+12;}
        return 0;
    }
    for(unsigned i=0;i<t->count;i++){
        unsigned row=t->variant==2?(t->unlocked?104+i*108:(i?320:104)):t->extra?120+(t->extra_ids[i]-1)*108:t->unlocked?120+i*108:(i==0?120:i==1?(t->variant==1?228:336):444);
        if(t->selected==(int)i)row+=36;
        if(blend(screen,&t->parts,t->variant==2?176:48,(t->variant==2?280:208)+(int)i*48,t->extra?288:0,row,288,36))return -1;
    }
    if(t->variant==2)return blend(screen,&t->parts,0,108,0,0,640,104);
    return blend(screen,&t->parts,16,16,0,0,608,120);
}
int ktitle_hit(const KTitle *t,int x,int y){
    if(!t||!t->active)return -1;
    if(t->variant==4){
        if(x<452||x>=640)return -1;
        for(unsigned i=0;i<t->count;i++)if(y>=260+(int)i*36&&y<292+(int)i*36)return (int)i;
        return -1;
    }
    if(t->variant==3){int top=280;if(x<176||x>=464)return -1;for(unsigned i=0;i<t->count;i++){int h=t->extra_ids[i]==2?48:36;if(y>=top&&y<top+h)return (int)i;top+=h+12;}return -1;}
    int left=t->variant==2?176:48,top=t->variant==2?280:208;
    if(x<left||x>=left+288)return -1;
    for(unsigned i=0;i<t->count;i++)if(y>=top+(int)i*48&&y<top+36+(int)i*48)return (int)i;
    return -1;
}
void ktitle_free(KTitle *t){if(!t)return;rmt_free(&t->background);rmt_free(&t->parts);memset(t,0,sizeof(*t));}

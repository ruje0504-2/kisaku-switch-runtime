#include "flag_dialog.h"
#include <string.h>
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int kflag_dialog_areas(KFlagDialog *d,const uint8_t *data,size_t size){
    if(!d||!data||size!=144||le32(data)!=7)return -1;
    int32_t areas[7][5];
    for(unsigned i=0;i<7;i++){
        for(unsigned j=0;j<5;j++)areas[i][j]=(int32_t)le32(data+4+(i*5+j)*4);
        if(areas[i][0]!=(i<3?(int)i:0x100+(int)i-3)||areas[i][1]<0||areas[i][2]<0||areas[i][3]>640||areas[i][4]>480||areas[i][1]>=areas[i][3]||areas[i][2]>=areas[i][4])return -1;
    }
    memcpy(d->areas,areas,sizeof(areas));return 0;
}
static int copy(KImage *dst,const KImage *src,unsigned dx,unsigned dy,unsigned sx,unsigned sy,unsigned w,unsigned h){
    if(!src->pixels||(uint64_t)dx+w>dst->width||(uint64_t)dy+h>dst->height||(uint64_t)sx+w>src->width||(uint64_t)sy+h>src->height)return -1;
    for(unsigned y=0;y<h;y++)memcpy(dst->pixels+(dy+y)*dst->stride+dx*4,src->pixels+(sy+y)*src->stride+sx*4,w*4);
    return 0;
}
int kflag_dialog_draw(const KFlagDialog *d,KImage *screen){
    if(!d||!screen||!screen->pixels||screen->width!=640||screen->height!=480)return -1;
    if(copy(screen,&d->background,0,0,0,0,640,480))return -1;
    unsigned any=0;
    for(unsigned i=0;i<4;i++){
        any|=d->checked[i]==1;
        unsigned state=d->checked[i]<2&&(d->checked[i]||d->selected==0x100+(int)i);
        if(copy(screen,&d->parts,55,120+i*52,0,84+i*104+state*52,528,52)||copy(screen,&d->parts,152,134+i*52,d->checked[i]==1?24:0,60,24,24))return -1;
        if(d->checked[i]==2)for(unsigned y=120+i*52;y<172+i*52;y++)for(unsigned x=55;x<583;x++){uint8_t *p=screen->pixels+y*screen->stride+x*4;for(unsigned c=0;c<3;c++)p[c]=(uint8_t)(p[c]/2);}
    }
    const unsigned x[]={152,256,408},w[]={72,132,84};
    for(unsigned i=0;i<3;i++){
        unsigned state=i==0&&!any?3:d->selected==(int)i?1:0;
        if(copy(screen,&d->parts,x[i],344,state*w[i],i*20,w[i],20))return -1;
    }
    return 0;
}
int kflag_dialog_hit(const KFlagDialog *d,int x,int y){
    if(!d||!d->active)return -1;
    for(unsigned i=0;i<7;i++){const int32_t *a=d->areas[i];if(x>=a[1]&&x<a[3]&&y>=a[2]&&y<a[4])return a[0];}
    return -1;
}
void kflag_dialog_move(KFlagDialog *d,int dx,int dy){
    if(!d||!d->active||(!dx&&!dy))return;
    int s=d->selected;
    /* Native 428e60..429010: vertical checkbox list and horizontal buttons. */
    if(s<0)s=0;
    else if(dy<0)s=s<0x100?0x103:s==0x100?0:s-1;
    else if(dy>0)s=s<0x100?s:s==0x103?0:s+1;
    else if(s<0x100){if(dx<0&&s>0)s--;if(dx>0&&s<2)s++;}
    d->selected=s;
}
void kflag_dialog_free(KFlagDialog *d){if(!d)return;rmt_free(&d->background);rmt_free(&d->parts);memset(d,0,sizeof(*d));}

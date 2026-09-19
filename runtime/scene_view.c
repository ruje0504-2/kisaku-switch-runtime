#include "scene_view.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static int visible(const KSceneNode *n){return n->width>0&&n->height>0;}
int kscene_move(const KScene *s,unsigned selected,int dx,int dy){
    if(!s||s->count>KSCENE_COUNT||selected>=s->count||!visible(&s->nodes[selected])||(!dx&&!dy))return -1;
    const KSceneNode *from=&s->nodes[selected];int best=-1;int64_t score=INT64_MAX;
    for(unsigned i=0;i<s->count;i++){
        const KSceneNode *n=&s->nodes[i];if(i==selected||!visible(n))continue;
        int64_t x=(int64_t)n->x-from->x,y=(int64_t)n->y-from->y;
        int64_t along=dx?x*dx:y*dy,across=dx?llabs(y):llabs(x);if(along<=0)continue;
        /* Prefer the same branch, but allow crossing into another column. */
        int64_t cost=along+across*4;if(cost<score){score=cost;best=(int)i;}
    }
    return best<0?(int)selected:best;
}
static void fill(KImage *out,int x,int y,int w,int h,unsigned color){
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++){
        if(xx<0||yy<0||xx>=(int)out->width||yy>=(int)out->height)continue;
        uint8_t *p=out->pixels+(size_t)yy*out->stride+(size_t)xx*4;
        p[0]=(uint8_t)color;p[1]=(uint8_t)(color>>8);p[2]=(uint8_t)(color>>16);p[3]=255;
    }
}
static void tile(KImage *out,const KImage *src,int dx,int dy,int sx,int sy,int keyed){
    for(int y=0;y<42;y++)for(int x=0;x<60;x++){
        if(dx+x<0||dy+y<0||dx+x>=640||dy+y>=480)continue;
        const uint8_t *p=src->pixels+(size_t)(sy+y)*src->stride+(size_t)(sx+x)*4;
        if(keyed&&p[0]==0&&p[1]==255&&p[2]==0)continue;
        memcpy(out->pixels+(size_t)(dy+y)*out->stride+(size_t)(dx+x)*4,p,4);
    }
}
#include "scene_links.inc"
#include "scene_highlight.inc"
/* Draw native connection sprites before thumbnails, preserving node order. */
static void links(const KScene *s,const KImage *parts,int left,int top,int64_t maxx,int64_t maxy,unsigned overview,KImage *out){
    unsigned style=0;
    for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<s->count;i++){
        const KSceneNode *node=&s->nodes[i];if(!visible(node))continue;
        unsigned id=node->id-1;
        unsigned begin=pass?highlight_offsets[id]:route_offsets[id],end=pass?highlight_offsets[id+1]:route_offsets[id+1];
        for(unsigned n=begin;n<end;n++){
            unsigned flag=pass?highlight_sprites[n].flag:route_sprites[n].flag,value=s->visited[flag];
            if(!value||(pass&&value==1))continue;
            if(pass&&value==2)style=1;
            unsigned glyph=pass?highlight_sprites[n].glyph:route_sprites[n].glyph;
            int x=node->x+(pass?highlight_sprites[n].x:route_sprites[n].x),y=node->y+(pass?highlight_sprites[n].y:route_sprites[n].y);
            int w=pass?highlight_glyphs[glyph].w:route_glyphs[glyph].w,h=pass?highlight_glyphs[glyph].h:route_glyphs[glyph].h;
            if(overview){
                int dx=16+(int)((int64_t)x*608/maxx),dy=16+(int)((int64_t)y*448/maxy);
                int right=16+(int)(((int64_t)x+w)*608/maxx),bottom=16+(int)(((int64_t)y+h)*448/maxy);
                fill(out,dx,dy,right>dx?right-dx:1,bottom>dy?bottom-dy:1,pass?0x70e0ff:0x8095a8);continue;
            }
            x-=left;y-=top;if(x>=640||x+w<=0||y>=480||y+h<=0)continue;
            int sx=pass?highlight_glyphs[glyph].x:route_glyphs[glyph].x,sy=pass?highlight_glyphs[glyph].y+42*(int)style:route_glyphs[glyph].y;
            for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
                int dx=x+xx,dy=y+yy;if(dx<0||dy<0||dx>=640||dy>=480)continue;
                memcpy(out->pixels+(size_t)dy*out->stride+(size_t)dx*4,parts->pixels+(size_t)(sy+yy)*parts->stride+(size_t)(sx+xx)*4,4);
            }
        }
    }
}
int kscene_view(const KScene *s,const KImage *atlas,const KImage *parts,unsigned selected,unsigned overview,KImage *out){
    if(!s||s->count>KSCENE_COUNT||selected>=s->count||!visible(&s->nodes[selected])||!atlas||!parts||!atlas->pixels||!parts->pixels||atlas->width<1200||atlas->height<798||parts->width<232||parts->height<137||!out)return -1;
    if(!out->pixels){*out=(KImage){0,0,640,480,2560,calloc(640*480,4)};if(!out->pixels)return -1;}
    if(out->width!=640||out->height!=480||out->stride<2560)return -1;
    fill(out,0,0,640,480,0x101820);
    const KSceneNode *focus=&s->nodes[selected];int64_t maxx=0,maxy=0;
    for(unsigned i=0;i<s->count;i++){
        const KSceneNode *n=&s->nodes[i];if(!visible(n))continue;
        if(n->x<0||n->y<0||n->x>100000||n->y>100000||n->width!=60||n->height!=42||n->order>=380||n->id<1||n->id>KSCENE_COUNT)return -1;
        if(n->x+60>maxx)maxx=n->x+60;
        if(n->y+42>maxy)maxy=n->y+42;
    }
    int left=focus->x-290,top=focus->y-219;
    if(left<0)left=0;
    if(top<0)top=0;
    links(s,parts,left,top,maxx,maxy,overview,out);
    for(unsigned i=0;i<s->count;i++){
        const KSceneNode *n=&s->nodes[i];if(!visible(n))continue;
        unsigned state=s->status[n->id-1];
        if(overview){
            int x=16+(int)((int64_t)n->x*608/maxx),y=16+(int)((int64_t)n->y*448/maxy);
            fill(out,x,y,14,5,state==6?0x60d090:state?0xa4b8ce:0x425264);
            if(i==selected){fill(out,x-2,y-2,18,2,0xffe878);fill(out,x-2,y+5,18,2,0xffe878);}
            continue;
        }
        int x=n->x-left,y=n->y-top;if(x>=640||x+60<=0||y>=480||y+42<=0)continue;
        /* 44e490: original record order, not sorted scene ID, selects atlas tile. */
        if(state==1||state==5||state==6)tile(out,atlas,x,y,(int)(n->order%20)*60,(int)(n->order/20)*42,0);
        if(!state)tile(out,parts,x,y,52,0,0);
        else if(state==1||state==2)tile(out,parts,x,y,112,0,1);
        else if(state==5)tile(out,parts,x,y,112,46,1);
        else if(state==6)tile(out,parts,x,y,172,0,1);
        if(i==selected){fill(out,x-2,y-2,64,2,0xffe878);fill(out,x-2,y+42,64,2,0xffe878);fill(out,x-2,y,2,42,0xffe878);fill(out,x+60,y,2,42,0xffe878);}
    }
    return 0;
}

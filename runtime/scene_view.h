#ifndef KISAKU_SCENE_VIEW_H
#define KISAKU_SCENE_VIEW_H
#include "scene.h"
#include "rmt.h"
/* Geometry uses the original map.map coordinate system. */
int kscene_move(const KScene *s,unsigned selected,int dx,int dy);
int kscene_view(const KScene *s,const KImage *atlas,const KImage *parts,unsigned selected,unsigned overview,KImage *out);
#endif

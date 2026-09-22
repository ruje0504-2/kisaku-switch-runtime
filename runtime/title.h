#ifndef KISAKU_TITLE_H
#define KISAKU_TITLE_H
#include "rmt.h"
typedef struct {
    KImage background,parts;
    unsigned count,age,active,variant;
    int selected,unlocked;
    int native_ids[6];
    unsigned native_sources[6];
    unsigned extra,extra_ids[5],main_count;
} KTitle;
int ktitle_draw(const KTitle *t,KImage *screen);
int ktitle_hit(const KTitle *t,int x,int y);
void ktitle_free(KTitle *t);
#endif

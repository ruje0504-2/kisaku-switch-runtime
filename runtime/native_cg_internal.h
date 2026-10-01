#ifndef KISAKU_NATIVE_CG_INTERNAL_H
#define KISAKU_NATIVE_CG_INTERNAL_H

#include "bootstrap.h"

/* Private ownership shared by the implementation and runtime tests. */
typedef struct KNativeCG {
    KImage base,screen,parts,atlas,variants;
    unsigned alternate,group,page,view,item,variant;
    int focus;KBootstrap *player;char problem[256];
} KNativeCG;

#endif

#ifndef KISAKU_FLAG_DIALOG_H
#define KISAKU_FLAG_DIALOG_H
#include "rmt.h"
typedef struct {
    KImage background,parts;
    int selected;
    unsigned active,checked[4];
    int32_t areas[7][5];
} KFlagDialog;
int kflag_dialog_areas(KFlagDialog *d,const uint8_t *data,size_t size);
int kflag_dialog_draw(const KFlagDialog *d,KImage *screen);
int kflag_dialog_hit(const KFlagDialog *d,int x,int y);
void kflag_dialog_move(KFlagDialog *d,int dx,int dy);
void kflag_dialog_free(KFlagDialog *d);
#endif

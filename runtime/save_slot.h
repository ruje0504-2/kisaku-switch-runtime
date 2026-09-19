#ifndef KISAKU_SAVE_SLOT_H
#define KISAKU_SAVE_SLOT_H
#include "flags.h"
#include "rmt.h"
#include "control_store.h"
/* A manifest switches both immutable files together. Original saves are read only. */
int kslot_write(const char *root,unsigned selector,unsigned slot,const KFlags *flags,const KControlRecord *records,unsigned count);
int kslot_read(const char *root,unsigned selector,unsigned slot,KFlags **flags,KControlStore **controls);
/* Optional thumbnail belongs to the same committed generation as the save. */
int kslot_write_image(const char *root,unsigned selector,unsigned slot,const KFlags *flags,const KControlRecord *records,unsigned count,const KImage *image);
int kslot_preview(const char *root,unsigned selector,unsigned slot,KImage *image,int64_t *saved_time);
typedef struct {unsigned scene;int64_t saved_time;char comment[43];} KSlotInfo;
int kslot_info(const KFlags *flags,unsigned slot,KSlotInfo *info);
#endif

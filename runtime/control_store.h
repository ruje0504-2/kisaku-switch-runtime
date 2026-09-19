#ifndef KISAKU_CONTROL_STORE_H
#define KISAKU_CONTROL_STORE_H
#include "vm.h"
typedef struct { uint16_t id,type; KValue *values; unsigned count; } KControlRecord;
typedef struct KControlStore {
    struct KControlStore *next;
    KControlRecord records[29];unsigned count;
} KControlStore;
KControlStore *kcontrol_read(const char *path);
int kcontrol_write(const char *path,const KControlRecord *records,unsigned count);
KControlStore *kcontrol_read_slot(const char *root,unsigned selector,unsigned slot);
int kcontrol_write_slot(const char *root,unsigned selector,unsigned slot,const KControlRecord *records,unsigned count);
void kcontrol_free(KControlStore *store);
#endif

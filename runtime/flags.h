#ifndef KISAKU_FLAGS_H
#define KISAKU_FLAGS_H
#include "vm.h"
typedef struct KFlags {
    struct KFlags *next;
    uint8_t module[260];
    KValue *globals[2]; unsigned counts[2];
    uint8_t *bytes,*raw;uint16_t *words;
    unsigned byte_count,word_count,raw_count;
} KFlags;
/* Native FLAG files: module[260], typed bank0, bytes, words, typed bank1, raw. */
KFlags *kflags_read(const char *path);
void kflags_free(KFlags *f);
int kflags_write(const KFlags *f,const char *path);
/* Kisaku 5080a0: merge current progress into an existing snapshot. */
int kflags_merge(KFlags *saved,const KFlags *current);
/* Isolated FLAG000 progress; preserve other native fields, merge byte maxima. */
int kflags_progress(const char *root,unsigned selector,const uint8_t *bytes,unsigned count);
int kflags_write_slot(const KFlags *f,const char *root,unsigned selector,unsigned slot);
KFlags *kflags_read_slot(const char *root,unsigned selector,unsigned slot);
#endif

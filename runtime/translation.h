#ifndef KISAKU_TRANSLATION_H
#define KISAKU_TRANSLATION_H

#include "text_encoding.h"

typedef struct {
    uint32_t *source;
    size_t source_count;
    uint32_t *target;
    size_t target_count;
} KTranslationEntry;
typedef struct { KTranslationEntry *entries; size_t count; } KTranslation;

int ktranslation_load(KTranslation *translation, const char *path);
void ktranslation_free(KTranslation *translation);
void ktranslation_apply(const KTranslation *translation, KTextChar *chars, size_t count);

#endif

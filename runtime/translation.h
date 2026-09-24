#ifndef KISAKU_TRANSLATION_H
#define KISAKU_TRANSLATION_H

#include "text_encoding.h"

typedef struct {
    uint32_t *source;
    size_t source_count;
    uint32_t *target;
    size_t target_count;
} KTranslationEntry;
typedef struct {
    KTranslationEntry *entries;
    size_t count;
    size_t *buckets;
    size_t bucket_count;
    size_t max_source_count;
} KTranslation;

/* Runtime-panel strings are keyed separately from script phrases.  Keeping
 * them in their own loose file lets UI wording change without rebuilding the
 * game script translation table or touching the original artwork. */
typedef struct {
    char *key;
    char *value;
} KUiTranslationEntry;
typedef struct {
    KUiTranslationEntry *entries;
    size_t count;
} KUiTranslation;

int ktranslation_load(KTranslation *translation, const char *path);
void ktranslation_free(KTranslation *translation);
int ktranslation_apply(const KTranslation *translation, KTextChar *chars, size_t *count, size_t capacity);

int kui_translation_load(KUiTranslation *translation, const char *path);
void kui_translation_free(KUiTranslation *translation);
const char *kui_translation_get(const KUiTranslation *translation, const char *key);
const char *kui_translation_default(const char *key);

#endif

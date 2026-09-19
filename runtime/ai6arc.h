#ifndef KISAKU_AI6ARC_H
#define KISAKU_AI6ARC_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef struct { char name[261]; uint32_t packed, size, offset; } Ai6Entry;
typedef struct {
    FILE *file;
    uint32_t count;
    Ai6Entry *entries;
    /* Archive directory and file stem, used to locate loose override files. */
    char dir[2048];
    char stem[64];
} Ai6Archive;
int ai6_open(Ai6Archive *arc, const char *path);
void ai6_close(Ai6Archive *arc);
int ai6_read(Ai6Archive *arc, uint32_t index, uint8_t **data, size_t *size);
/* Resolve a resource by name, preferring a loose override file over the archive
 * entry. Returns 0 with malloc'd data on success, -1 when neither exists. */
int ai6_read_named(Ai6Archive *arc, const char *name, uint8_t **data, size_t *size);
/* Locate the loose override file for a resource name; 0 on success. */
int ai6_override_path(const Ai6Archive *arc, const char *name, char *out, size_t outn);
#endif

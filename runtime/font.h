#ifndef KISAKU_FONT_H
#define KISAKU_FONT_H
#include "rmt.h"
typedef struct KFont KFont;
typedef struct {
    size_t glyphs,bitmap_bytes,glyph_limit,bitmap_limit;
    uint64_t hits,rasterizations;
} KFontCacheStats;
/* A user font path, or platform font (macOS / Switch shared font). */
KFont *kfont_open(const char *path,int simplified);
void kfont_close(KFont *font);
/* Read-only diagnostics for the per-font, colour-independent LRU cache.
   Bitmap bytes exclude fixed hash buckets and bounded entry metadata. */
int kfont_cache_stats(const KFont *font,KFontCacheStats *stats);
int kfont_draw(KFont *font,KImage *dst,uint32_t codepoint,int x,int y,unsigned width,unsigned height,uint32_t rgb);
/* Draw a glyph with a thin black core and faint outer edge. Used only by the
   supplied story/choice font; HOS UI panels continue using plain glyphs. */
int kfont_draw_outline(KFont *font,KImage *dst,uint32_t codepoint,int x,int y,
                       unsigned width,unsigned height,uint32_t rgb);
#endif

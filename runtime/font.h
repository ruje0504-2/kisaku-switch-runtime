#ifndef KISAKU_FONT_H
#define KISAKU_FONT_H
#include "rmt.h"
typedef struct KFont KFont;
/* A user font path, or platform font (macOS / Switch shared font). */
KFont *kfont_open(const char *path,int simplified);
void kfont_close(KFont *font);
int kfont_draw(KFont *font,KImage *dst,uint32_t codepoint,int x,int y,unsigned width,unsigned height,uint32_t rgb);
/* Draw a glyph with a one-pixel black outline.  This is used only by the
   supplied story/choice font; HOS UI panels continue using plain glyphs. */
int kfont_draw_outline(KFont *font,KImage *dst,uint32_t codepoint,int x,int y,
                       unsigned width,unsigned height,uint32_t rgb);
#endif

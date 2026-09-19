#ifndef KISAKU_FONT_H
#define KISAKU_FONT_H
#include "rmt.h"
typedef struct KFont KFont;
/* A user font path, or platform font (macOS / Switch shared font). */
KFont *kfont_open(const char *path,int simplified);
void kfont_close(KFont *font);
int kfont_draw(KFont *font,KImage *dst,uint32_t codepoint,int x,int y,unsigned width,unsigned height,uint32_t rgb);
#endif

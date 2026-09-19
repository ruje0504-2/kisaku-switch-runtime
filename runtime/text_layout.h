#ifndef KISAKU_TEXT_LAYOUT_H
#define KISAKU_TEXT_LAYOUT_H
#include "text_encoding.h"
typedef struct {int x,y;} KTextPosition;
/* Native 4886e0 kinsoku layout. right is a cursor boundary; closing
   punctuation may hang one full character beyond it. This bounded wrapper detects page overflow.
   Returns -1 for invalid input, 1 for a page overflow, 0 on success. */
int ktext_layout(const KTextChar *chars,size_t count,int left,int top,int right,int bottom,int half,int line,int *x,int *y,KTextPosition *positions);
/* 4886e0/437390 do not test the bottom edge: the image surface clips glyphs.
   Preserve the resulting off-surface cursor for subsequent script commands. */
int ktext_layout_native(const KTextChar *chars,size_t count,int left,int top,int right,int bottom,int half,int line,int *x,int *y,KTextPosition *positions);
#endif

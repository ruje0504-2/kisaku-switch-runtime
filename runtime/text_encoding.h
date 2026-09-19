#ifndef KISAKU_TEXT_ENCODING_H
#define KISAKU_TEXT_ENCODING_H
#include <stdint.h>
#include <stddef.h>
typedef enum { KTEXT_CP932,KTEXT_GBK } KTextEncoding;
typedef struct {uint32_t codepoint;unsigned columns;} KTextChar;
/* Strict, bounded decoding. On failure count is zero; caller ignores output. */
int ktext_decode(KTextEncoding encoding,const uint8_t *input,size_t size,KTextChar *out,size_t capacity,size_t *count);
int ktext_name_encode(const char *input,uint8_t out[33],size_t *size);
/* True when the decoded run contains private-use codepoints (U+E000..U+F8FF).
 * CP932 maps undefined lead bytes into that area, so a translation-encoded
 * script decoded as CP932 shows up here instead of failing outright. */
int ktext_private_use(const KTextChar *chars,size_t count);
/* Decode with the configured encoding; with auto_fallback, retry the other
 * encoding when the configured one fails or yields private-use codepoints, and
 * report the encoding actually used. */
int ktext_decode_auto(KTextEncoding configured,int auto_fallback,const uint8_t *input,size_t size,
                      KTextChar *out,size_t capacity,size_t *count,KTextEncoding *used);
#endif

/* GPL-2.0-or-later. Host probe: feed the text runs of a translated MES script
 * through the runtime's own strict decoder with both CP932 and GBK, to show what
 * happens when TextEncoding does not match the patch. */
#include "text_encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int high(unsigned char c) { return c >= 0x81; }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <script.mes>\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f);

    unsigned runs = 0, cp932_ok = 0, cp932_fail = 0, auto_ok = 0, auto_fail = 0, switched = 0;
    unsigned shown = 0;
    for (long i = 0; i < n; ) {
        if (!high(d[i])) { i++; continue; }
        long start = i;
        unsigned pairs = 0;
        while (i + 1 < n && high(d[i]) && d[i + 1] >= 0x40 && d[i + 1] != 0x7f) { i += 2; pairs++; }
        if (pairs < 2) { i = start + 1; continue; }
        size_t size = (size_t)(i - start);
        /* the runtime stops the string at the first NUL, so trim the same way */
        for (size_t k = 0; k < size; k++) if (!d[start + k]) { size = k; break; }
        KTextChar out[1024]; size_t count = 0;
        /* Candidate text = a run that at least one table decodes cleanly. */
        int as_gbk = ktext_decode(KTEXT_GBK, d + start, size, out, 1024, &count);
        int as_cp932 = ktext_decode(KTEXT_CP932, d + start, size, out, 1024, &count);
        if (as_gbk && as_cp932) continue;
        runs++;
        if (as_cp932) cp932_fail++; else cp932_ok++;
        KTextEncoding used = KTEXT_CP932;
        if (ktext_decode_auto(KTEXT_CP932, 1, d + start, size, out, 1024, &count, &used)) auto_fail++;
        else { auto_ok++; if (used == KTEXT_GBK) switched++; }
        if (shown < 4) {
            printf("  msg @0x%05lx bytes=%zu  CP932=%-4s auto=%-4s\n", start, size,
                   as_cp932 ? "ok" : "FAIL", auto_fail && !auto_ok ? "?" : (used == KTEXT_GBK ? "GBK" : "CP932"));
            printf("      raw: ");
            for (size_t k = 0; k < (size < 24 ? size : 24); k++) printf("%02x", d[start + k]);
            printf("\n");
            shown++;
        }
    }
    printf("\n%s\n", argv[1]);
    printf("  candidate text runs : %u\n", runs);
    printf("  CP932 (default)     : %u ok, %u FAIL\n", cp932_ok, cp932_fail);
    printf("  auto (CP932 base)   : %u ok, %u FAIL, %u switched to GBK\n", auto_ok, auto_fail, switched);
    free(d);
    return cp932_fail ? 1 : 0;
}

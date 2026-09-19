/* GPL-2.0-or-later. Host probe: report how a resource name resolves for a real
 * archive plus optional loose override files, so patch drop-ins can be verified
 * without repacking. Usage: override-probe <archive> <resource-name> */
#include "ai6arc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fnv(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <archive> <name>\n", argv[0]); return 2; }
    Ai6Archive a = {0};
    if (ai6_open(&a, argv[1])) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    printf("archive : %s\n", argv[1]);
    printf("  dir   : %s\n", a.dir);
    printf("  stem  : %s\n", a.stem);
    printf("  entries: %u\n", a.count);

    char path[4096];
    if (ai6_override_path(&a, argv[2], path, sizeof(path)) == 0) printf("override: %s\n", path);
    else printf("override: (none)\n");

    /* Archive entry alone, for comparison. */
    for (uint32_t i = 0; i < a.count; i++) {
        const char *x = argv[2], *y = a.entries[i].name;
        int same = 1;
        while (*x && *y) {
            int cx = (*x >= 'A' && *x <= 'Z') ? *x + 32 : *x;
            int cy = (*y >= 'A' && *y <= 'Z') ? *y + 32 : *y;
            if (cx != cy) { same = 0; break; }
            x++; y++;
        }
        if (same && !*x && !*y) {
            uint8_t *d = NULL; size_t n = 0;
            if (!ai6_read(&a, i, &d, &n)) { printf("arc only : %s size=%zu fnv=%016llx\n", a.entries[i].name, n, (unsigned long long)fnv(d, n)); free(d); }
            break;
        }
    }

    uint8_t *d = NULL; size_t n = 0;
    if (ai6_read_named(&a, argv[2], &d, &n)) printf("resolved : FAILED\n");
    else {
        printf("resolved : size=%zu fnv=%016llx\n", n, (unsigned long long)fnv(d, n));
        if (n >= 4) printf("  magic  : %.4s\n", (const char *)d);
        if (n >= 8 && !memcmp(d, "VSD1", 4)) {
            uint32_t skip = (uint32_t)d[4] | (uint32_t)d[5] << 8 | (uint32_t)d[6] << 16 | (uint32_t)d[7] << 24;
            printf("  VSD1 prefix=%u -> stream at %u (%s)\n", skip, 8 + skip,
                   (8 + skip < n && d[8 + skip] == 0 && d[9 + skip] == 0 && d[10 + skip] == 1) ? "MPEG pack ok" : "check");
        }
        free(d);
    }
    ai6_close(&a);
    return 0;
}

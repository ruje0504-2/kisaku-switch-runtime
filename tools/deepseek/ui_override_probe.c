/* GPL-2.0-or-later. Host probe: exercise the frontend's own ui_asset() helper
 * (tools/native_ui.inc) so the loose-override wiring used by the save / settings
 * / backlog panels is verified end to end, not just ai6_read_named in isolation.
 * Usage: ui-override-probe <ELFIMAGE dir> <asset name>...
 * One line per asset: name, resolved override path (or "-"), decoded size and
 * FNV of the pixels, so two roots (archive-only vs patch-installed) can be
 * diffed to prove every panel asset really comes from the loose file. */
#include "ai6arc.h"
#include "rmt.h"
#include "bootstrap.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only ui_asset/ui_canvas are used here; the remaining inline helpers in the
 * panel header are dropped by the compiler. */
#include "../native_ui.inc"

static uint64_t fnv(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <ELFIMAGE> <asset name>...\n", argv[0]); return 2; }
    KBootstrap *b = calloc(1, sizeof(*b));
    if (!b) return 1;
    char path[2048];
    snprintf(path, sizeof(path), "%s/rmt.arc", argv[1]);
    if (ai6_open(&b->images, path)) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    snprintf(b->root, sizeof(b->root), "%s", argv[1]);

    printf("root       : %s\n", argv[1]);
    for (int i = 2; i < argc; i++) {
        const char *name = argv[i];
        char loose[4096];
        int has_override = ai6_override_path(&b->images, name, loose, sizeof(loose)) == 0;
        KImage image = {0};
        if (ui_asset(b, name, &image)) {
            printf("%-22s FAILED  override=%s\n", name, has_override ? loose : "-");
            continue;
        }
        size_t bytes = (size_t)image.stride * image.height;
        printf("%-22s %s %ux%u fnv=%016llx\n", name, has_override ? "LOOSE " : "ARCHV ",
               (unsigned)image.width, (unsigned)image.height,
               (unsigned long long)fnv(image.pixels, bytes));
        rmt_free(&image);
    }
    ai6_close(&b->images);
    free(b);
    return 0;
}

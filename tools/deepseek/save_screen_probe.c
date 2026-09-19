/* GPL-2.0-or-later. Host probe: composite the save/load screen artwork exactly
 * as tools/save_menu.inc does (same atlas rectangles, minus the glyph layer),
 * so the loose-override effect on the save panel is visible as an image rather
 * than only as a hash. Usage: save-screen-probe <ELFIMAGE dir> <out.ppm> */
#include "ai6arc.h"
#include "rmt.h"
#include "bootstrap.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../native_ui.inc"

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <ELFIMAGE> <out.ppm>\n", argv[0]); return 2; }
    KBootstrap *b = calloc(1, sizeof(*b));
    if (!b) return 1;
    char path[2048];
    snprintf(path, sizeof(path), "%s/rmt.arc", argv[1]);
    if (ai6_open(&b->images, path)) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    snprintf(b->root, sizeof(b->root), "%s", argv[1]);

    KImage artwork = {0};
    if (ui_asset(b, "saveload_p.rmt", &artwork)) { fprintf(stderr, "saveload_p.rmt failed\n"); return 1; }
    KImage canvas = {0};
    if (ui_canvas(&canvas)) return 1;

    /* save_menu_draw state: save mode, slot 1 (page 0 / row 0), first group,
       no focus, save allowed. */
    int page = 0, row = 0, focus = 0;
    ui_fill(&canvas, 0, 0, 640, 480, 0x16100b);
    ui_blit(&canvas, &artwork, 0, 0, 640, 28, 0, 0, 0);
    ui_blit(&canvas, &artwork, 0, 30, 68, 28, 4, 0, 0);
    ui_blit(&canvas, &artwork, 164 + page * 44, 80, 40, 22, 164 + page * 44, 3, 0);
    ui_blit(&canvas, &artwork, 0, 294, 344, 272, 8, 30, 0);
    for (int i = 0; i < 6; i++) {
        int sx = i < 3 ? 166 + i * 150 : 166 + (i - 3) * 150;
        int sy = i < 3 ? 104 : 200;
        if (focus && i == 1) sy += 48;
        ui_blit(&canvas, &artwork, sx, sy, 150, 24, 10, 316 + i * 26, 0);
    }
    for (int i = 0; i < 10; i++) {
        ui_blit(&canvas, &artwork, 348, i == row && !focus ? 346 : 302, 274, 44, 364, 35 + i * 44, 0);
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror("open"); return 1; }
    fprintf(out, "P6\n640 480\n255\n");
    for (size_t i = 0; i < 640 * 480; i++) {
        uint8_t *p = canvas.pixels + i * 4;
        uint8_t rgb[3] = {p[2], p[1], p[0]};
        if (fwrite(rgb, 1, 3, out) != 3) return 1;
    }
    fclose(out);

    char loose[4096];
    int over = ai6_override_path(&b->images, "saveload_p.rmt", loose, sizeof(loose)) == 0;
    printf("saveload_p.rmt artwork: %ux%u from %s\n", (unsigned)artwork.width, (unsigned)artwork.height,
           over ? loose : "rmt.arc");
    printf("wrote %s\n", argv[2]);
    rmt_free(&canvas);
    rmt_free(&artwork);
    ai6_close(&b->images);
    free(b);
    return 0;
}

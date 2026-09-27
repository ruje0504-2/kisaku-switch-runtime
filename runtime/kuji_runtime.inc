/* CKuji modal runtime: logic-first slice of 31/710.  KKuji owns no heap
 * memory, so there is nothing to release at shutdown. */
/* CKuji modal runtime: logic-first slice of 31/710.
 *
 * The native handler is synchronous: it runs an interactive modal
 * (ISelect::run through the inherited vtable slot 0x68) until the player
 * confirms a column, then pushes the resolved prize and returns.  The port
 * drives the same shape as the bowling runtime: the dispatch sets the modal
 * up, bootstrap_run() reports "busy" while kkuji_active() is set, and the
 * result is pushed exactly once when the walk finishes. */
#ifndef KISAKU_KUJI_RUNTIME_INCLUDED
#define KISAKU_KUJI_RUNTIME_INCLUDED

static int kkuji_begin(KBootstrap *b, const unsigned pool[4]) {
    if (b->kuji.active) return error(b, "31/710 already running (arguments preserved)");
    KKuji *k = &b->kuji;
    for (unsigned i = 0; i < 4; i++) {
        if (pool[i] > 9) return error(b, "31/710 prize id range (arguments preserved)");
        k->pool[i] = pool[i];
    }
    k->pool[4] = KKUJI_BLANK;
    kkuji_shuffle(k, &b->vm->random_state);
    k->select = 0;
    k->result = KKUJI_BLANK;
    k->chosen = k->drawn = k->clock = 0;
    k->active = 1;
    /* kuji.mes pre-loads kuji_pt.akb into layer 5 and kuji_c.akb into layer 9
       through 19/1; the engine's vtable+0xbc fetch (5/9) maps to those layers.
       The background/column rendering is the next slice. */
    return 0;
}

static int kkuji_active(const KBootstrap *b) { return b->kuji.active; }

/* Pointer hover: the five hit rectangles derived from the native initialiser
   (cx-60, cy-98, cx+60, cy+48) with cy = 600. */
static int kkuji_pointer(KBootstrap *b, int x, int y, unsigned held) {
    KKuji *k = &b->kuji;
    if (!k->active || k->chosen) return 0;
    for (unsigned i = 0; i < KKUJI_COLUMNS; i++) {
        int cx = kkuji_column_x[i];
        if (x >= cx - 60 && x < cx + 60 && y >= 600 - 98 && y < 600 + 48) {
            k->select = i;
            if (held) { k->chosen = 1; k->clock = 0; }
            return 1;
        }
    }
    return 0;
}

/* Confirmation (ISelect vtable slot 27 = 0x48f850): start the walk. */
static int kkuji_confirm(KBootstrap *b) {
    KKuji *k = &b->kuji;
    if (!k->active) return 0;
    if (!k->chosen) { k->chosen = 1; k->clock = 0; }
    return 0;
}

/* Per-frame: after the confirmation the native walks the fixed polyline
   (20 ms per step) and waits one more second before handing the prize back. */
static int kkuji_frame(KBootstrap *b) {
    KKuji *k = &b->kuji;
    if (!k->active) return 0;
    if (!k->chosen) return 0;
    k->clock += KKUJI_STEP_MS;
    if (!k->drawn) k->drawn = 1;      /* kujip{select+1}.akb request point */
    if (k->clock < KKUJI_TAIL_MS) return 0;
    k->result = k->slot[kkuji_perm[k->select]];
    if (kvm_push(b->vm, (KValue){(int32_t)k->result, NULL})) {
        k->active = 0;
        return error(b, "31/710 result push failed");
    }
    k->active = 0;
    return 0;
}

/* Presentation for the logic-first slice: the AKB column/result rendering
   (native vtable+0xbc fetch of layers 5/9 plus kujip{select+1}.akb) is the
   next slice.  Until then draw only the selection cursor so the modal is
   usable and the automated probe can drive it. */
static void kkuji_present(KBootstrap *b) {
    KKuji *k = &b->kuji;
    if (!k->active || !b->layers[0].pixels) return;
    int cx = kkuji_column_x[k->select] - 54, cy = 600 - 92;
    for (int y = 0; y < 40; y++) {
        int dy = cy + y;
        if (dy < 0 || dy >= 480) continue;
        uint8_t *row = b->layers[0].pixels + (size_t)dy * b->layers[0].stride;
        for (int x = 0; x < 40; x++) {
            int dx = cx + x;
            if (dx < 0 || dx >= 640) continue;
            uint8_t *px = row + (size_t)dx * 4;
            px[0] = 255; px[1] = 255; px[2] = 0; px[3] = 255;
        }
    }
}

#endif

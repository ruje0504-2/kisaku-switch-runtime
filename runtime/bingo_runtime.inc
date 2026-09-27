/* CBingo modal runtime: 31/611.  Same shape as kuji/hummer: the dispatch
   starts the modal, bootstrap_run() reports busy while kbingo_active() is
   set, one ball is drawn per KBINGO_WAIT_MS (250 ms while fast-forwarding),
   and the single integer result (1 = line completed, 0 = none) is pushed
   exactly once.  KBingo owns no heap memory. */
#ifndef KISAKU_BINGO_RUNTIME_INCLUDED
#define KISAKU_BINGO_RUNTIME_INCLUDED

static int kbingo_begin(KBootstrap *b) {
    if (b->bingo.active) return error(b, "31/611 already running (arguments preserved)");
    KBingo *k = &b->bingo;
    *k = (KBingo){0};
    kbingo_deal(k, &b->vm->random_state);
    k->active = 1;
    k->sound = 0;
    return 0;
}

static int kbingo_active(const KBootstrap *b) { return b->bingo.active; }

/* Shift/Ctrl (native [vt+0xa8], key codes 0x102/0x103) fast-forward at 4x. */
static int kbingo_confirm(KBootstrap *b) { b->bingo.fast = 1; b->bingo.clock = KBINGO_WAIT_MS; return 0; }
static int kbingo_pointer(KBootstrap *b, int x, int y, unsigned held) { (void)b; (void)x; (void)y; (void)held; return 0; }

static int kbingo_frame(KBootstrap *b, unsigned ms) {
    KBingo *k = &b->bingo;
    if (!k->active) return 0;
    unsigned step = k->fast ? KBINGO_FASTFORWARD_MS : KBINGO_WAIT_MS;
    k->clock += ms;
    while (k->clock >= step && k->drawn < KBINGO_BALLS) {
        k->clock -= step;
        kbingo_draw(k, k->pool[k->drawn]);
        k->drawn++;
        k->sound = 1;
        if (k->lines) break;
    }
    /* Two trailing 1000 ms beats before the result is handed back. */
    if (k->lines || k->drawn >= KBINGO_BALLS) k->tail += ms;
    if (k->tail < 2 * KBINGO_WAIT_MS) return 0;
    if (k->settled) return 0;
    k->result = k->lines ? 1u : 0u;
    k->settled = 1;
    if (kvm_push(b->vm, (KValue){(int32_t)k->result, NULL})) {
        k->active = 0;
        return error(b, "31/611 result push failed");
    }
    k->active = 0;
    return 0;
}

/* Logic-first presentation slice: the bingo_bg (layer 2), bingo_logo (5),
   bingo_parts digit atlas (9) and bingo_an.ax animation composition is the
   next slice; until then draw the marked/lined state as a small grid so the
   round is readable. */
static void kbingo_present(KBootstrap *b) {
    KBingo *k = &b->bingo;
    if (!k->active || !b->layers[0].pixels) return;
    for (unsigned cell = 0; cell < KBINGO_SIDE * KBINGO_SIDE; cell++) {
        unsigned row = cell / KBINGO_SIDE, col = cell % KBINGO_SIDE;
        int ox = 44 + (int)col * 64, oy = 52 + (int)row * 64;
        uint8_t r = k->hit[cell] ? 255 : 40, g = k->hit[cell] ? 220 : 40, bl = 40;
        for (int y = 0; y < 24; y++) {
            int dy = oy + y;
            if (dy < 0 || dy >= 480) continue;
            uint8_t *rowp = b->layers[0].pixels + (size_t)dy * b->layers[0].stride;
            for (int x = 0; x < 32; x++) {
                int dx = ox + x;
                if (dx < 0 || dx >= 640) continue;
                uint8_t *px = rowp + (size_t)dx * 4;
                px[0] = r; px[1] = g; px[2] = bl; px[3] = 255;
            }
        }
    }
}
#endif

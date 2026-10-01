/* CBingo modal runtime: 31/611.  Same shape as kuji/hummer: the dispatch
   starts the modal, bootstrap_run() reports busy while kbingo_active() is
   set, one ball is drawn per KBINGO_WAIT_MS (250 ms while fast-forwarding),
   and the single integer result (1 = line completed, 0 = none) is pushed
   exactly once.  KBingo owns no heap memory. */
#ifndef KISAKU_BINGO_RUNTIME_INCLUDED
#define KISAKU_BINGO_RUNTIME_INCLUDED

static void kbingo_release_art(KBingo *k){rmt_free(&k->background);rmt_free(&k->logo);rmt_free(&k->parts);rmt_free(&k->animation);}

static int kbingo_begin(KBootstrap *b) {
    if (b->bingo.active) return error(b, "31/611 already running (arguments preserved)");
    KBingo *k = &b->bingo;
    kbingo_release_art(k);
    *k = (KBingo){0};
    if (mini_load_akb(b,"bingo_bg.akb",&k->background)||
        mini_load_akb(b,"bingo_logo.akb",&k->logo)||
        mini_load_akb(b,"bingo_parts.akb",&k->parts)||
        mini_load_akb(b,"bingo_an.akb",&k->animation)){
        kbingo_release_art(k);return error(b,"31/611 artwork decode failed (arguments preserved)");
    }
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
    /* Native 4d7e60 first presents six logo rows (two 20 ms ticks each),
       then alternates logo rows y=440/528 for 60 three-tick beats. */
    if (k->lines || k->drawn >= KBINGO_BALLS) k->tail += ms;
    unsigned settle_ms = k->lines ? KBINGO_SUCCESS_MS : 2 * KBINGO_WAIT_MS;
    if (k->tail < settle_ms) return 0;
    if (k->settled) return 0;
    k->result = k->lines ? 1u : 0u;
    k->settled = 1;
    if (kvm_push(b->vm, (KValue){(int32_t)k->result, NULL})) {
        k->active = 0;
        kbingo_release_art(k);
        return error(b, "31/611 result push failed");
    }
    k->active = 0;
    kbingo_release_art(k);
    return 0;
}

static void kbingo_present(KBootstrap *b) {
    KBingo *k = &b->bingo;
    if (!k->active || !b->layers[0].pixels) return;
    mini_clear(&b->layers[0]);
    mini_blit(&b->layers[0],&k->background,0,0,0,0,640,480);
    /* bingo_an.akb is a 7x7 sheet of 240x268 dial frames.  The native
       animator places the selected frame over the dial in bingo_bg. */
    if(k->animation.pixels){
        unsigned frame=(k->clock/40u)%49u;
        mini_blit_key(&b->layers[0],&k->animation,376,16,(int)(frame%7u)*240,(int)(frame/7u)*268,240,268,0x00ff00);
    }
    for (unsigned cell = 0; cell < KBINGO_SIDE * KBINGO_SIDE; cell++) {
        unsigned row = cell / KBINGO_SIDE, col = cell % KBINGO_SIDE;
        if(row==2&&col==2)continue;
        int ox = 44 + (int)col * 64, oy = 52 + (int)row * 64;
        unsigned number=k->card[cell],sx=(number%20u)*32u,sy=(number/20u)*24u+(k->hit[cell]?48u:0u);
        mini_blit_key(&b->layers[0],&k->parts,ox,oy,(int)sx,(int)sy,32,24,0x00ff00);
    }
    if (k->lines && k->logo.pixels) {
        unsigned source_y;
        if (k->tail < KBINGO_SUCCESS_INTRO_MS) {
            unsigned row = k->tail / (KBINGO_SUCCESS_ROW_TICKS * KBINGO_TICK_MS);
            if (row >= KBINGO_SUCCESS_ROWS) row = KBINGO_SUCCESS_ROWS - 1;
            source_y = row * 88u;
        } else {
            unsigned tick = (k->tail - KBINGO_SUCCESS_INTRO_MS) / KBINGO_TICK_MS;
            source_y = (tick & 1u) ? 528u : 440u;
        }
        /* The native result surface is 640x88 and is composited centered. */
        mini_blit_key(&b->layers[0], &k->logo, 0, 196, 0, (int)source_y,
                      640, 88, 0x00ff00);
    }
}
#endif

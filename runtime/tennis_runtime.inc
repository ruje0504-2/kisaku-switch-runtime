/* CTennis modal runtime: 31/610.  The native match runs by itself (no input),
   so the port advances one point per KTENNIS_POINT_MS and pushes the single
   0/1 result when the best-of-five match is decided. */
#ifndef KISAKU_TENNIS_RUNTIME_INCLUDED
#define KISAKU_TENNIS_RUNTIME_INCLUDED

static unsigned ktennis_difficulty(KBootstrap *b) {
    int difficulty = option(b, "Difficult", "AllMiniGame", 1);   /* 0x4647c0 */
    if (difficulty < 0) difficulty = 0;
    if (difficulty > 2) difficulty = 2;
    return (unsigned)difficulty;
}

static int ktennis_begin(KBootstrap *b) {
    if (b->tennis.active) return error(b, "31/610 already running (arguments preserved)");
    KTennis *t = &b->tennis;
    *t = (KTennis){0};
    t->difficulty = ktennis_difficulty(b);
    t->active = 1;
    return 0;
}

static int ktennis_active(const KBootstrap *b) { return b->tennis.active; }

/* Confirm skips the rally (the native keeps no interactive input; this is the
   port's fast-forward for length reasons and is documented as such). */
static int ktennis_confirm(KBootstrap *b) { b->tennis.fast = 1; return 0; }
static int ktennis_pointer(KBootstrap *b, int x, int y, unsigned held) { (void)b; (void)x; (void)y; (void)held; return 0; }

static void ktennis_point(KBootstrap *b, KTennis *t) {
    /* Player point probability falls as the configured difficulty rises
       (d0 0.55, d1 0.50, d2 0.45); the ball trajectory tables in the native
       are 30.0/32.0/40.0 for difficulty 0/1/2. */
    unsigned roll = kkuji_rand(&b->vm->random_state, 100);
    unsigned threshold = t->difficulty == 0 ? 55u : t->difficulty == 1 ? 50u : 45u;
    int player_point = roll < threshold;
    if (player_point) t->points_player++; else t->points_cpu++;
    t->points_played++;
    unsigned a = t->points_player, c = t->points_cpu;
    int game_over = 0;
    if (a >= KTENNIS_POINTS_TO_WIN && a - c >= 2) game_over = 1;
    if (c >= KTENNIS_POINTS_TO_WIN && c - a >= 2) game_over = 1;
    if (!game_over) return;
    if (a > c) t->games_player++; else t->games_cpu++;
    t->points_player = t->points_cpu = 0;
    t->server ^= 1u;                     /* native flips [+0x104] each game */
}

static int ktennis_frame(KBootstrap *b, unsigned ms) {
    KTennis *t = &b->tennis;
    if (!t->active) return 0;
    unsigned step = t->fast ? KTENNIS_FASTFORWARD_MS : KTENNIS_POINT_MS;
    t->clock += ms;
    while (t->clock >= step) {
        t->clock -= step;
        ktennis_point(b, t);
        if (t->games_player >= KTENNIS_GAMES_TO_WIN || t->games_cpu >= KTENNIS_GAMES_TO_WIN) break;
    }
    if (t->games_player < KTENNIS_GAMES_TO_WIN && t->games_cpu < KTENNIS_GAMES_TO_WIN) return 0;
    if (t->settled) return 0;
    t->result = t->games_player > t->games_cpu ? 1u : 0u;
    t->settled = 1;
    if (kvm_push(b->vm, (KValue){(int32_t)t->result, NULL})) {
        t->active = 0;
        return error(b, "31/610 result push failed");
    }
    t->active = 0;
    return 0;
}

/* Logic-first presentation slice: the six layer.arc atlases
   (tennisbg/tennisp1/tennisc1/tennisc2/tennisp2/tennis_p), the eight 0xd0-byte
   sprite objects and the frame-number tables are the next slice; until then
   draw the running score so the match is readable. */
static void ktennis_present(KBootstrap *b) {
    KTennis *t = &b->tennis;
    if (!t->active || !b->layers[0].pixels) return;
    unsigned cells = t->games_player + t->games_cpu + t->points_player + t->points_cpu;
    for (unsigned i = 0; i < cells && i < 40; i++) {
        int ox = 40 + (int)(i % 20) * 16, oy = 40 + (int)(i / 20) * 16;
        for (int y = 0; y < 12; y++) {
            int dy = oy + y; if (dy < 0 || dy >= 480) continue;
            uint8_t *row = b->layers[0].pixels + (size_t)dy * b->layers[0].stride;
            for (int x = 0; x < 12; x++) {
                int dx = ox + x; if (dx < 0 || dx >= 640) continue;
                uint8_t *px = row + (size_t)dx * 4;
                px[0] = 255; px[1] = 255; px[2] = 0; px[3] = 255;
            }
        }
    }
}
#endif

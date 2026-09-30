/* CHummer modal runtime: 31/711.
 *
 * Shaped like the kuji/bowling runtimes: the dispatch sets the modal up,
 * bootstrap_run() reports busy while khammer_active() is set, every frame
 * advances the ms clock, and the single integer result (1 = success,
 * 0 = failure) is pushed exactly once.  KHummer owns no heap memory. */
#ifndef KISAKU_HUMMER_RUNTIME_INCLUDED
#define KISAKU_HUMMER_RUNTIME_INCLUDED

static void khummer_release_art(KHummer *h){rmt_free(&h->background);rmt_free(&h->holes);rmt_free(&h->effects);}

/* GetConfig("AllMiniGame","Difficult") clamped to 0..2 (0x4647c0); the same
   key the tennis engine reads, with the bowling default of 1. */
static unsigned khammer_difficulty(KBootstrap *b) {
    int difficulty = option(b, "Difficult", "AllMiniGame", 1);   /* same key as bowling */
    if (difficulty < 0) difficulty = 0;
    if (difficulty > 2) difficulty = 2;
    return (unsigned)difficulty;
}

static int khammer_begin(KBootstrap *b) {
    if (b->hummer.active) return error(b, "31/711 already running (arguments preserved)");
    KHummer *h = &b->hummer;
    khummer_release_art(h);
    if (mini_load_akb(b,"hummbg.akb",&h->background)||
       mini_load_akb(b,"hummp.akb",&h->holes)||
       mini_load_akb(b,"hummps.akb",&h->effects)){khammer_release_art(h);return error(b,"31/711 artwork decode failed (arguments preserved)");}
    h->active = 1;
    h->clock = h->hole_clock = h->effect_clock = h->reveal_clock = 0;
    h->hole = h->revealed = h->settled = 0;
    h->result = 0;
    h->difficulty = khammer_difficulty(b);
    h->last_x = h->last_y = 0;
    h->have_last = h->last_sign_x = h->last_sign_y = 0;
    h->meter = 0.0;
    h->hammer_t = h->hammer_h = 0.0;
    h->sound = 0;                      /* meter.wav */
    return 0;
}

static int khammer_active(const KBootstrap *b) { return b->hummer.active; }

/* Cursor movement: the native only compares GetCursorPos deltas and counts a
   *direction flip* (sign change) as one unit of charge, so a back-and-forth
   wiggle fills the meter while a steady drag does not. */
static int khammer_pointer(KBootstrap *b, int x, int y, unsigned held) {
    (void)held;
    KHummer *h = &b->hummer;
    if (!h->active || h->settled) return 0;
    if (!h->have_last) { h->last_x = x; h->last_y = y; h->have_last = 1; return 1; }
    int dx = x - h->last_x, dy = y - h->last_y;
    int flip_x = dx ? (h->last_sign_x && ((dx > 0) != (h->last_sign_x > 0))) : 0;
    int flip_y = dy ? (h->last_sign_y && ((dy > 0) != (h->last_sign_y > 0))) : 0;
    double charge = (flip_x || flip_y) ? 1.0 : 0.0;
    /* The native also adds |dx| on difficulty 0 (0x4a4a9c..0x4a4b16). */
    if (h->difficulty == 0 && flip_x) charge += (double)(dx < 0 ? -dx : dx);
    if (h->meter + charge > 100.0) h->meter = 100.0; else h->meter += charge;
    if (dx) h->last_sign_x = dx;
    if (dy) h->last_sign_y = dy;
    h->last_x = x; h->last_y = y;
    return 1;
}

/* Confirmation (ISelect vtable slot 27 forwarded by 0x40b280) starts the
   10 s round; the native blocks the framework loop while 0x4a4440 runs. */
static int khammer_confirm(KBootstrap *b) { (void)b; return 0; }

/* Per-frame: `ms` elapsed since the previous frame (the caller passes the
   same 20 ms quantum the native uses for its settle step). */
static int khammer_frame(KBootstrap *b, unsigned ms) {
    KHummer *h = &b->hummer;
    if (!h->active) return 0;
    if (!ms) return 0;
    unsigned before = h->clock;
    h->clock += ms;
    h->hole_clock += ms;
    h->effect_clock += ms;
    h->reveal_clock += ms;
    /* 300 ms: the mole alternates between holes 0 and 1 only (no rand()). */
    while (h->hole_clock >= KHUMMER_HOLE_MS) {
        h->hole_clock -= KHUMMER_HOLE_MS;
        h->hole = (h->hole + 1) & 1u;
    }
    while (h->effect_clock >= KHUMMER_EFFECT_MS) h->effect_clock -= KHUMMER_EFFECT_MS;
    /* 100 ms per revealed hole; gun.wav fires when the 8th is shown. */
    while (h->reveal_clock >= KHUMMER_REVEAL_MS && h->revealed < KHUMMER_HOLES) {
        h->reveal_clock -= KHUMMER_REVEAL_MS;
        h->revealed++;
        if (h->revealed == 8) h->sound = 1;      /* gun.wav */
    }
    /* Meter decay, per difficulty, applied per elapsed millisecond. */
    {
        unsigned seg = (unsigned)(h->meter * KHUMMER_SEGMENTS / 100.0);
        if (seg > KHUMMER_SEG_MAX) seg = KHUMMER_SEG_MAX;
        double decay = h->difficulty == 2 ? seg * 0.0009 + 0.03
                     : h->difficulty == 1 ? seg * 0.0009 + 0.01
                                          : seg * 0.00045 + 0.01;
        double amount = decay * (double)ms;
        h->meter = h->meter > amount ? h->meter - amount : 0.0;
    }
    /* Hammer parabola: t += 0.2 every 20 ms, h = meter*t - 4.9*t*t. */
    h->hammer_t += 0.2 * ((double)ms / (double)KHUMMER_STEP_MS);
    h->hammer_h = h->meter * h->hammer_t - 4.9 * h->hammer_t * h->hammer_t;
    if (h->hammer_h < 0.0) h->hammer_h = 0.0;
    if (h->clock < KHUMMER_LIMIT_MS) return 0;
    if (h->settled) return 0;
    /* 0x4a5086 settlement: seg >= 30 wins. */
    unsigned seg = (unsigned)(h->meter * KHUMMER_SEGMENTS / 100.0);
    if (seg > KHUMMER_SEG_MAX) seg = KHUMMER_SEG_MAX;
    h->result = seg >= KHUMMER_THRESHOLD ? 1u : 0u;
    if (h->result) { h->meter *= 0.92; h->sound = 3; }   /* bingofun.wav */
    else { h->meter *= 0.87; h->sound = 2; }             /* bell.wav */
    h->settled = 1;
    if (kvm_push(b->vm, (KValue){(int32_t)h->result, NULL})) {
        h->active = 0;
        khammer_release_art(h);
        return error(b, "31/711 result push failed");
    }
    (void)before;
    h->active = 0;
    khammer_release_art(h);
    return 0;
}

static void khammer_present(KBootstrap *b) {
    KHummer *h = &b->hummer;
    if (!h->active || !b->layers[0].pixels) return;
    mini_clear(&b->layers[0]);
    mini_blit(&b->layers[0],&h->background,0,0,0,0,640,480);
    if (h->holes.pixels) {
        const KHummerHole *hole=&khammer_holes[h->hole%KHUMMER_HOLES];
        mini_blit_key(&b->layers[0],&h->holes,132,64,hole->x,hole->y,376,368,0x00ff00);
    }
    if (h->effects.pixels) {
        unsigned frame=(h->effect_clock/KHUMMER_EFFECT_MS)%3;
        mini_blit_key(&b->layers[0],&h->effects,218,40,(int)frame*204,0,204,248,0x00ff00);
    }
}

#endif

/* CStaff modal runtime: 31/210 (staff roll end screen).
 *
 * Contract (0x4fb9e0): exactly one integer operand = mode
 * (staffroll.mes passes 1 for the full 11-segment end_p01..p11.ax roll,
 * staffroll_s.mes and the inline sc_end04.mes call pass 0 for the short
 * end_p1m..p11m set), and there is NO VM write-back: the handler only
 * maintains the "native modal owns the screen" byte, bytes[4012] = 1 on
 * entry and 0 on exit.  Returning to the title is the caller's job
 * (nazo19_z.mes does `22/1 "staffroll.mes"` then `22/0 "open.mes"`).
 *
 * The native builds .?AVCStaff@@ (vtable 0x5480ec), starts imomushi.wav
 * (0x469c33) and runs 24 steps around an independent CFuncAnime instance
 * ([0x5d3c60]) whose eleven AX segments total about 2100 ticks at 20 ms
 * (~42 s), scrolling 44 pre-rendered subtitle pages (pages < 27 live in
 * layer 2, the rest in layer 3) through a 256x136 window at (192,296) with
 * [this+0xdc] += 4 and a -640 wrap.
 *
 * Slice status: the operand contract, the bytes[4012] ownership flag and the
 * segment timing are implemented; the 44-page subtitle scroll and the AX
 * track playback are the next slice (the page tables at 0x56c118.. are not
 * yet extracted). */
#ifndef KISAKU_STAFFROLL_RUNTIME_INCLUDED
#define KISAKU_STAFFROLL_RUNTIME_INCLUDED

#define KSTAFF_FULL_TICKS 2100
#define KSTAFF_SHORT_TICKS 1050
#define KSTAFF_TICK_MS 20

static int kstaff_begin(KBootstrap *b, int mode) {
    if (b->staffroll.active) return error(b, "31/210 already running (arguments preserved)");
    if (mode != 0 && mode != 1) return error(b, "31/210 mode must be 0 or 1 (arguments preserved)");
    KStaffroll *s = &b->staffroll;
    *s = (KStaffroll){0};
    s->mode = (unsigned)mode;
    s->ticks = mode ? KSTAFF_FULL_TICKS : KSTAFF_SHORT_TICKS;
    s->active = 1;
    /* The native modal marks the screen as owned by a native subsystem. */
    if ((unsigned)4012 < b->vm->byte_count) b->vm->bytes[4012] = 1;
    return 0;
}

static int kstaff_active(const KBootstrap *b) { return b->staffroll.active; }
static int kstaff_confirm(KBootstrap *b) { (void)b; return 0; }
static int kstaff_pointer(KBootstrap *b, int x, int y, unsigned held) { (void)b; (void)x; (void)y; (void)held; return 0; }

static int kstaff_frame(KBootstrap *b, unsigned ms) {
    KStaffroll *s = &b->staffroll;
    if (!s->active) return 0;
    s->clock += ms;
    while (s->clock >= KSTAFF_TICK_MS && s->elapsed < s->ticks) {
        s->clock -= KSTAFF_TICK_MS;
        s->elapsed++;
        if (s->elapsed % 4 == 0) {          /* [this+0xdc] += 4, -640 wrap */
            s->scroll += 4;
            if (s->scroll >= 640) s->scroll -= 640;
        }
    }
    if (s->elapsed < s->ticks) return 0;
    s->active = 0;
    if ((unsigned)4012 < b->vm->byte_count) b->vm->bytes[4012] = 0;
    return 0;
}

static void kstaff_present(KBootstrap *b) {
    KStaffroll *s = &b->staffroll;
    if (!s->active || !b->layers[0].pixels) return;
    /* Until the 44 subtitle pages are extracted, show the elapsed progress as
       a steadily filling bar so the sequence is visibly running. */
    unsigned width = (unsigned)((uint64_t)s->elapsed * 600u / (s->ticks ? s->ticks : 1u));
    for (int y = 0; y < 8; y++) {
        int dy = 460 + y; if (dy < 0 || dy >= 480) continue;
        uint8_t *row = b->layers[0].pixels + (size_t)dy * b->layers[0].stride;
        for (unsigned x = 0; x < width && x < 640; x++) {
            uint8_t *px = row + (size_t)x * 4;
            px[0] = 255; px[1] = 255; px[2] = 255; px[3] = 255;
        }
    }
}
#endif

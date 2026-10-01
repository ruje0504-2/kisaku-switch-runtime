/* CStaff modal runtime: 31/210 (staff roll end screen).
 *
 * Contract (0x4fb9e0): exactly one integer operand = mode
 * (staffroll.mes passes 1 for the full end roll, staffroll_s.mes and the
 * inline sc_end04.mes call pass 0 for the short end roll), and there is NO
 * VM write-back: the handler only
 * maintains the "native modal owns the screen" byte, bytes[4012] = 1 on
 * entry and 0 on exit.  Returning to the title is the caller's job
 * (nazo19_z.mes does `22/1 "staffroll.mes"` then `22/0 "open.mes"`).
 *
 * The native builds .?AVCStaff@@ (vtable 0x5480ec), starts imomushi.wav
 * (0x469c33) and runs 24 steps around an independent CFuncAnime instance
 * ([0x5d3c60]) whose twelve AX resources total about 2100 ticks at 20 ms
 * (~42 s), scrolling 44 pre-rendered subtitle pages (pages < 27 live in
 * layer 2, the rest in layer 3) through a 256x136 window at (192,296) with
 * [this+0xdc] += 4 and a -640 wrap.
 *
 * The AX files are played in the native resource order.  Both rolls include
 * a distinct sixth transition resource (end_p06s/end_p6ms), followed by the
 * regular p06 resource.  The page artwork is already present in the layer archive; the
 * renderer below scrolls the matching pre-rendered atlas through the native
 * 256x136 window.
 */
#ifndef KISAKU_STAFFROLL_RUNTIME_INCLUDED
#define KISAKU_STAFFROLL_RUNTIME_INCLUDED

#define KSTAFF_FULL_TICKS 2100
#define KSTAFF_SHORT_TICKS 1050
#define KSTAFF_TICK_MS 20

static const char *const kstaff_full_segments[] = {
    "end_p01.ax", "end_p02.ax", "end_p03.ax", "end_p04.ax",
    "end_p05.ax", "end_p06s.ax", "end_p06.ax", "end_p07.ax",
    "end_p08.ax", "end_p09.ax", "end_p10.ax", "end_p11.ax"
};
static const char *const kstaff_short_segments[] = {
    "end_p1m.ax", "end_p2m.ax", "end_p3m.ax", "end_p4m.ax",
    "end_p5m.ax", "end_p6ms.ax", "end_p6m.ax", "end_p7m.ax",
    "end_p8m.ax", "end_p9m.ax", "end_p10m.ax", "end_p11m.ax"
};

static void kstaff_release_art(KStaffroll *s){
    rmt_free(&s->background);rmt_free(&s->part1);rmt_free(&s->part2);rmt_free(&s->part3);
    rmt_free(&s->staff1);rmt_free(&s->staff2);
}

static int kstaff_load_segment(KBootstrap *b, KStaffroll *s, unsigned segment) {
    const char *const *names = s->mode ? kstaff_full_segments : kstaff_short_segments;
    if (segment >= s->segment_count) return 0;
    const char *name = names[segment];
    uint8_t *data = NULL; size_t size = 0;
    if (read_named(&b->data, name, &data, &size) || !ax_load(&s->ax, name, data, size)) {
        free(data);
        return 0;
    }
    free(data);
    memset(s->ax_events, 0, sizeof(s->ax_events));
    /* The ending script installs endpart1 on layer 8 and endpart2 on
       layer 9.  The first eight tracks address the former atlas; the last
       track(s) address the latter.  Keep this selection with the AX track,
       rather than treating every descriptor as a part1 rectangle. */
    s->source_part = segment >= 8 ? 2u : 1u;
    return ax_control(&s->ax, 1, 0, 0) ? 1 : 0;
}

static int kstaff_begin(KBootstrap *b, int mode) {
    if (b->staffroll.active) return error(b, "31/210 already running (arguments preserved)");
    if (mode != 0 && mode != 1) return error(b, "31/210 mode must be 0 or 1 (arguments preserved)");
    KStaffroll *s = &b->staffroll;
    kstaff_release_art(s);
    *s = (KStaffroll){0};
    s->mode = (unsigned)mode;
    s->ticks = mode ? KSTAFF_FULL_TICKS : KSTAFF_SHORT_TICKS;
    s->segment_count = (unsigned)(sizeof(kstaff_full_segments) / sizeof(kstaff_full_segments[0]));
    /* CStaff starts at -640 (0xFFFFFD80), then advances the active
       subtitle layer by four pixels on every 20 ms update. */
    s->scroll = -640;
    if(mini_load_akb(b,"endbg.akb",&s->background)||mini_load_akb(b,mode?"endpart1.akb":"endpart1m.akb",&s->part1)||
       mini_load_akb(b,mode?"endpart2.akb":"endpart2m.akb",&s->part2)||
       mini_load_akb(b,"staff1.akb",&s->staff1)||mini_load_akb(b,"staff2.akb",&s->staff2)){
        kstaff_release_art(s);return error(b,"31/210 ending resources decode failed (arguments preserved)");
    }
    if(mode&&!mini_load_akb(b,"endpart3.akb",&s->part3)){}
    if (!kstaff_load_segment(b, s, 0)){
        kstaff_release_art(s);return error(b,"31/210 ending AX start failed (arguments preserved)");
    }
    s->active = 1;
    /* The native modal marks the screen as owned by a native subsystem. */
    if ((unsigned)4012 < b->vm->byte_count) b->vm->bytes[4012] = 1;
    return 0;
}

static int kstaff_active(const KBootstrap *b) { return b->staffroll.active; }
static int kstaff_confirm(KBootstrap *b) { (void)b; return 0; }
static int kstaff_pointer(KBootstrap *b, int x, int y, unsigned held) { (void)b; (void)x; (void)y; (void)held; return 0; }

static void kstaff_base(KBootstrap *b, KStaffroll *s) {
    mini_clear(&b->layers[0]);
    mini_blit(&b->layers[0],&s->background,0,0,0,0,640,480);
    /* The native CStaff moves one of the two subtitle layers at
       (scroll,236); the other layer is not composited over the frame. */
    const KImage *staff=s->subtitle_page<27u?&s->staff1:&s->staff2;
    mini_blit_key(&b->layers[0],staff,s->scroll,236,0,0,640,480,0x00ff00);
    s->prepared=1;
}

static void kstaff_ax_draw(const uint32_t d[7],unsigned cell,void *context){
    (void)cell;KBootstrap *b=context;KStaffroll *s=&b->staffroll;
    if(d[0]>3)return;
    KImage *src=s->source_part==2u?&s->part2:&s->part1;
    int y=(int)d[6];if(d[0]==1&&y>479)y-=480;
    mini_blit_key(&b->layers[0],src,(int)d[5],y,(int)d[1],(int)d[2],d[3],d[4],0x00ff00);
}

static int kstaff_frame(KBootstrap *b, unsigned ms) {
    KStaffroll *s = &b->staffroll;
    if (!s->active) return 0;
    s->clock += ms;
    while (s->clock >= KSTAFF_TICK_MS && s->elapsed < s->ticks) {
        s->clock -= KSTAFF_TICK_MS;
        s->elapsed++;
        kstaff_base(b,s);
        if(!ax_tick_native(&s->ax,s->ax_events,kstaff_ax_draw,b)){
            s->active=0;kstaff_release_art(s);
            if ((unsigned)4012 < b->vm->byte_count) b->vm->bytes[4012] = 0;
            return error(b,"31/210 invalid ending AX instruction");
        }
        s->segment_clock++;
        unsigned count = s->segment_count;
        unsigned segment_ticks = s->ticks / count;
        if (segment_ticks && s->segment + 1 < count && s->segment_clock >= segment_ticks) {
            s->segment++;
            s->segment_clock = 0;
            if (!kstaff_load_segment(b, s, s->segment)) {
                s->active = 0;kstaff_release_art(s);
                if ((unsigned)4012 < b->vm->byte_count) b->vm->bytes[4012] = 0;
                return error(b, "31/210 ending AX segment decode failed");
            }
        }
        /* 0x468cdf: every CStaff update advances the signed position. */
        s->scroll += 4;
        if (s->scroll > 0) s->scroll = -640;
        s->subtitle_page=(s->elapsed*44u)/(s->ticks?s->ticks:1u);
    }
    if (s->elapsed < s->ticks) return 0;
    s->active = 0;
    kstaff_release_art(s);
    if ((unsigned)4012 < b->vm->byte_count) b->vm->bytes[4012] = 0;
    return 0;
}

static void kstaff_present(KBootstrap *b) {
    KStaffroll *s = &b->staffroll;
    if (!s->active || !b->layers[0].pixels) return;
    if(!s->prepared)kstaff_base(b,s);
}
#endif

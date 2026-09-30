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
 * The AX files are played as the native eleven (full) or nine (short)
 * segments.  The page artwork is already present in the layer archive; the
 * renderer below scrolls the matching pre-rendered atlas through the native
 * 256x136 window.
 */
#ifndef KISAKU_STAFFROLL_RUNTIME_INCLUDED
#define KISAKU_STAFFROLL_RUNTIME_INCLUDED

#define KSTAFF_FULL_TICKS 2100
#define KSTAFF_SHORT_TICKS 1050
#define KSTAFF_TICK_MS 20

static void kstaff_release_art(KStaffroll *s){
    rmt_free(&s->background);rmt_free(&s->part1);rmt_free(&s->part2);rmt_free(&s->part3);
    rmt_free(&s->staff1);rmt_free(&s->staff2);
}

static int kstaff_load_segment(KBootstrap *b, KStaffroll *s, unsigned segment) {
    char name[32];
    if (s->mode) snprintf(name, sizeof(name), "end_p%02u.ax", segment + 1);
    else snprintf(name, sizeof(name), "end_p%um.ax", segment + 1);
    uint8_t *data = NULL; size_t size = 0;
    if (read_named(&b->data, name, &data, &size) || !ax_load(&s->ax, name, data, size)) {
        free(data);
        return 0;
    }
    free(data);
    memset(s->ax_events, 0, sizeof(s->ax_events));
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
    mini_blit_key(&b->layers[0],&s->staff1,0,0,0,0,640,480,0x00ff00);
    mini_blit_key(&b->layers[0],&s->staff2,0,0,0,0,640,480,0x00ff00);
    s->prepared=1;
}

static void kstaff_ax_draw(const uint32_t d[7],unsigned cell,void *context){
    (void)cell;KBootstrap *b=context;KStaffroll *s=&b->staffroll;
    if(d[0]>3)return;
    KImage *src=&s->part1;
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
        if(!ax_tick_native(&s->ax,s->ax_events,kstaff_ax_draw,b))s->ax.wait_cell=0;
        s->segment_clock++;
        unsigned count = s->mode ? 11u : 9u;
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
        if (s->elapsed % 4 == 0) {          /* [this+0xdc] += 4, -640 wrap */
            s->scroll += 4;
            if (s->scroll >= 640) s->scroll -= 640;
        }
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

/* Package 1 staging-cadence fixture.
 *
 * The 演出基础层 primitives (31/17 black-band blink, 31/21 colour fade,
 * 31/30 canvas transition) are what every later interface package inherits.
 * Until now they were only covered indirectly, by pixel assertions inside
 * other suites, so "帧数和时长" was never stated as a number.
 *
 * This fixture drives each primitive through the real bootstrap frame loop
 * and pins the frame count, then prints it, so a review can compare the
 * measured cadence against the PC reference instead of re-deriving it.
 * It does not launch the PC program and it is not a Switch hardware test.
 */
#include "bootstrap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static KBootstrap *open_runtime(const char *root, const char *saves)
{
    KBootstrap *b = bootstrap_create_split(root, saves);
    assert(b && !b->error[0]);
    assert(bootstrap_run(b, 100000) >= 0);
    assert(b->layers[0].pixels && b->canvas.pixels);
    /* The startup script arms its own fade before the title settles; run it
       out so every fixture below starts from a clean overlay state. */
    unsigned frames = 0;
    while (b->fade_steps || b->helper_steps || b->blink_active || b->transition_steps) {
        assert(frames++ < 8192);
        bootstrap_frame(b);
    }
    return b;
}

/* main is read from v->syscall and sub is the stack top, so callers push the
 * arguments in reverse script order and the subcall last. */
static void dispatch_native(KBootstrap *b, int main_id, const int *args, unsigned count)
{
    b->error[0] = 0;
    b->vm->status = KVM_SYSCALL;
    b->vm->syscall = main_id;
    b->vm->sp = 0;
    for (unsigned i = 0; i < count; i++) assert(!kvm_push(b->vm, (KValue){args[i], NULL}));
    if (bootstrap_dispatch(b)) {
        fprintf(stderr, "native %d/%d rejected: %s\n", main_id, args[count - 1], b->error);
        abort();
    }
}

/* The overlay is only opaque when the band surface covers every pixel and the
 * fade alpha is fully in; that composite is what "200 ms of black" means. */
static int overlay_fully_black(const KBootstrap *b)
{
    if (!b->fade_visible || b->fade_alpha != 255) return 0;
    const uint8_t *p = b->fade_surface.pixels;
    assert(p);
    for (size_t i = 0; i < 640u * 480u; i++) if (p[i * 4 + 3] != 255) return 0;
    return 1;
}

/* 31/17 black bands close from both edges, hold, reopen. Ten closing steps of
 * 24 rows per edge; the last closing step is the frame that makes the overlay
 * fully black, so it belongs to both the closing ramp and the hold and the
 * phases do not simply add up.
 *
 * Measured here: 30 frames, 12 of them fully black (200 ms at 60 Hz) - the hold
 * matches the constant. What does not is the ramp: the 100-unit residual of
 * each 900-unit step accumulates, so the closing ramp lands its last two steps
 * in one frame. Whether the original re-arms the clock on every step (a clean
 * 31-frame ramp, one step per frame) is UNPROVEN - the address that used to be
 * cited for it is a reference-project address, see reports/porting.md (OPEN).
 * This fixture therefore pins today's behaviour; it is not evidence of parity. */
static void test_blink(KBootstrap *b)
{
    const int args[] = {17};
    dispatch_native(b, 31, args, 1);
    assert(b->blink_active);
    assert(b->fade_visible);
    unsigned frames = 0, black = 0;
    while (b->blink_active) {
        assert(frames < 4096);
        bootstrap_frame(b);
        frames++;
        if (overlay_fully_black(b)) black++;
    }
    printf("31/17 blink        : %u frames, %u fully black (hold %.0f ms at 60 Hz)\n",
           frames, black, black * 1000.0 / 60.0);
    /* Characterisation of current behaviour, not a parity claim: the hold is
       the full 200 ms, the closing ramp is one step short of one-step-per-frame
       and that is the OPEN question recorded in reports/porting.md. */
    assert(frames == 30);
    assert(black == 12);
    assert(!b->fade_visible && !b->fade_steps);
}

static void test_fade(KBootstrap *b)
{
    /* 31/21/0 consumes colour (a) then duration (c); a = -1 keeps the
     * allocated black surface and only fills an explicit colour otherwise. */
    const int in[] = {64, -1, 0};
    dispatch_native(b, 21, in, 3);
    unsigned steps = b->fade_steps;
    assert(steps == 64 && b->fade_visible && b->fade_alpha == 0);
    unsigned frames = 0;
    while (b->fade_steps) { assert(frames < 4096); bootstrap_frame(b); frames++; }
    printf("31/21/0 fade in    : %u frames for %u steps\n", frames, steps);
    assert(frames == steps && b->fade_alpha == 255);

    /* 31/21/1 consumes duration only and is ignored unless the overlay is
     * currently visible, which is exactly the state reached above. */
    const int out[] = {32, 1};
    dispatch_native(b, 21, out, 2);
    steps = b->fade_steps;
    assert(steps == 32 && b->fade_visible);
    frames = 0;
    while (b->fade_steps) { assert(frames < 4096); bootstrap_frame(b); frames++; }
    printf("31/21/1 fade out   : %u frames for %u steps\n", frames, steps);
    assert(frames == steps && b->fade_alpha == 0 && !b->fade_visible);

    /* A hidden overlay must not arm a new animation from the fade-out call. */
    dispatch_native(b, 21, out, 2);
    assert(!b->fade_steps && !b->fade_visible);
}

/* 31/30/0 dissolves the private canvas into page 0. Display/EffectSpeed and
 * flag 0x4000 scale the requested step count before the runtime stores it, so
 * the fixture measures twice: once exactly as configured, then once with both
 * scalings neutralised, which pins the arithmetic itself rather than the
 * player's speed preference. */
static void test_canvas_transition(KBootstrap *b)
{
    const int args[] = {48, 0};

    dispatch_native(b, 30, args, 2);
    unsigned configured = b->transition_steps;
    assert(configured >= 1);
    unsigned frames = 0;
    while (b->transition_steps) { assert(frames < 8192); bootstrap_frame(b); frames++; }
    printf("31/30/0 canvas     : %u frames for %u configured steps\n", frames, configured);
    assert(frames == configured && b->transition_frame == configured);

    int slot = -1;
    char saved[512];
    for (unsigned i = 0; i < b->setting_count; i++) {
        if (!strcmp(b->settings[i].section, "Display") && !strcmp(b->settings[i].key, "EffectSpeed")) {
            memcpy(saved, b->settings[i].value, sizeof(saved));
            slot = (int)i;
        }
    }
    if (slot >= 0) memcpy(b->settings[slot].value, "0", 2);
    int32_t flags = b->vm->globals[0][50].number;
    b->vm->globals[0][50].number = flags & ~0x4000;

    dispatch_native(b, 30, args, 2);
    unsigned steps = b->transition_steps;
    frames = 0;
    while (b->transition_steps) { assert(frames < 8192); bootstrap_frame(b); frames++; }
    printf("31/30/0 canvas     : %u frames for %u unscaled steps\n", frames, steps);
    assert(steps == 49 && frames == steps && b->transition_frame == steps);

    b->vm->globals[0][50].number = flags;
    if (slot >= 0) memcpy(b->settings[slot].value, saved, sizeof(saved));
}

static uint8_t *board_pixel(KBootstrap *b,unsigned x,unsigned y){
    return b->layers[0].pixels+y*b->layers[0].stride+x*4;
}

static void test_board(const char *root,const char *saves){
    KBootstrap *b=bootstrap_create_split(root,saves);assert(b&&!b->error[0]);
    b->layer_count=8;
    const unsigned ids[]={0,1,2,3,7};
    const uint8_t colors[]={31,17,59,83,101};
    for(unsigned i=0;i<5;i++){
        unsigned id=ids[i];rmt_free(&b->layers[id]);
        b->layers[id]=(KImage){0,0,640,480,2560,malloc(640*480*4)};
        assert(b->layers[id].pixels);
        memset(b->layers[id].pixels,colors[i],640*480*4);
    }
    /* Preflight must reject the formerly accepted 30-row scratch buffer. */
    b->layers[2].height=30;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;
    assert(!kvm_push(b->vm,(KValue){0,NULL})&&!kvm_push(b->vm,(KValue){523,NULL}));
    assert(bootstrap_dispatch(b)<0&&b->vm->sp==2&&!b->exec523_active);
    assert(board_pixel(b,32,92)[0]==31);
    b->layers[2].height=480;
    for(unsigned mode=0;mode<2;mode++){
        const int args[]={(int)mode,523};dispatch_native(b,31,args,2);
        assert(b->exec523_active&&!b->vm->sp);
        size_t ip=b->vm->ip;
        for(unsigned step=0;step<6;step++){
            unsigned row=mode?5-step:step;
            unsigned x=mode?592-112*row:32,y=mode?272-36*row:92;
            assert(board_pixel(b,x,y)[0]==101&&board_pixel(b,x,y)[3]==31);
            assert(board_pixel(b,x+8+112*row,y)[0]==101);
            if(row)assert(board_pixel(b,x+8,y+8)[0]==59);
            assert(b->exec523_step==step);
            assert(bootstrap_run(b,100)==1&&b->vm->ip==ip);
            b->input_events=0;bootstrap_confirm(b);bootstrap_cancel(b);
            bootstrap_pointer(b,50,400,1);bootstrap_message_action(b,6);
            assert(!b->input_events&&!b->message_request&&!bootstrap_can_save(b));
            bootstrap_frame(b);assert(!b->error[0]&&b->exec523_step==step);
            bootstrap_frame(b);assert(!b->error[0]&&b->exec523_step==step+1);
            assert(b->exec523_active==(step<5));
        }
    }
    for(unsigned y=92;y<288;y++)for(unsigned x=32;x<608;x++){
        assert(board_pixel(b,x,y)[0]==17&&board_pixel(b,x,y)[3]==31);
    }
    for(unsigned kind=0;kind<4;kind++){
        b->effect_fast=kind==0||kind==3;
        b->vm->globals[0][50].number=kind?0x8000:0;
        b->vm->bytes[4012]=kind>=2;
        const int args[]={0,523};dispatch_native(b,31,args,2);
        unsigned frames=0;
        while(b->exec523_active){assert(frames++<20);bootstrap_frame(b);assert(!b->error[0]);}
        assert(frames==(kind==2?12:6));
    }
    bootstrap_destroy(b);
    puts("31/523: six visible rows, fresh 20ms waits (12 host frames), Alpha, VM/input isolation, speed gates and scratch preflight: PASS");
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    KBootstrap *b = open_runtime(argv[1], argv[2]);
    /* Order matters: the startup script leaves the fade overlay visible, so
       the fade fixture runs first and hands a hidden overlay to the other
       two, which the native entry points require. */
    test_board(argv[1],argv[2]);
    test_fade(b);
    test_blink(b);
    test_canvas_transition(b);
    bootstrap_destroy(b);
    puts("Kisaku staging cadence: blink/fade/canvas frame counts and black hold duration: PASS");
    return 0;
}

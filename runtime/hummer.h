#ifndef KISAKU_HUMMER_H
#define KISAKU_HUMMER_H
#include <stddef.h>
#include <stdint.h>
/* CHummer (main=31 / sub=711), native handler 0x4f9d00 (thunk 0x4fdc2d).
 *
 * 0x4f9d00 is only construct -> run -> take result -> destruct; the engine is
 * the RTTI class .?AVCHummer@@ (vtable 0x5463ac, ctor 0x4a3b60, dtor
 * 0x4a3750) deriving from .?AVISelect@@ (vtable 0x547db4).  The main loop is
 * vtable+0x6c = 0x4a4440, reached through the inherited ISelect::run
 * (vtable+0x68) from the confirmation key.
 *
 * Contract: the script pushes nothing (hummer.mes+0x2dd is
 * `push 10; push 701; add; push 31; syscall`), the engine pushes one integer,
 * 1 = success / 0 = failure, written by 0x4a5751 (`sete cl`) into the result
 * KValue at CHummer+8 and handed to the VM by 0x4a3aa0 through 0x401f70 /
 * 0x4b7120.  hummer.mes copies it into global0[18] and byte[653]; it does not
 * branch on it.
 *
 * Rules (native constants, all ms based, timer 0x467c70 -> timeGetTime):
 *   10 s limit (1000 ms tick, initial count 10), mole every 300 ms at
 *   hole idx = (idx+1)&1 (only holes 0 and 1 alternate, no rand()), effect
 *   animation every 120 ms (3 frames of 204x248), holes revealed every
 *   100 ms, settle step 20 ms.
 *   The power meter is fed by *direction flips* of the cursor
 *   (GetCursorPos delta sign change, 0/1 per iteration) and decays per
 *   difficulty from GetConfig("AllMiniGame","Difficult") clamped to 0..2:
 *     seg = min((int)(meter * 36 / 100), 34)
 *     d2: seg*0.0009 + 0.03   d1: seg*0.0009 + 0.01   d0: seg*0.00045 + 0.01
 *   Settlement 0x4a5086: seg >= 30 -> success.  The hammer follows
 *   h = meter*t - 4.9*t*t with t += 0.2 every 20 ms and the power bar is
 *   drawn 330 - h long (minimum 50).
 *   Sounds: meter.wav on start, gun.wav at the 8th revealed hole, bell.wav
 *   when the hammer bottoms out, bingofun.wav on success. */
#define KHUMMER_LIMIT_MS 10000
#define KHUMMER_HOLE_MS 300
#define KHUMMER_EFFECT_MS 120
#define KHUMMER_REVEAL_MS 100
#define KHUMMER_STEP_MS 20
#define KHUMMER_SEGMENTS 36
#define KHUMMER_SEG_MAX 34
#define KHUMMER_THRESHOLD 30
#define KHUMMER_HOLES 13

/* hummp.akb cell grid (1504x1492, 4x4 of 376x368); the native animation
   table uses the first 13 cells as the hammer sequence. */
typedef struct KHummerFrame { int x, y; } KHummerFrame;
static const KHummerFrame khammer_frames[KHUMMER_HOLES] = {
    {0, 0}, {376, 0}, {752, 0}, {1128, 0}, {0, 368}, {376, 368}, {752, 368},
    {1128, 368}, {0, 736}, {376, 736}, {752, 736}, {1128, 736}, {0, 1104}
};

typedef struct KHummer {
    KImage background, frames, girl, tutorial;
    unsigned active;            /* modal owns the VM while set */
    unsigned tutorial_visible;  /* tuthum.akb remains until the start key */
    unsigned clock;             /* elapsed ms since the modal started */
    unsigned hole_clock;        /* 300 ms mole alternation */
    unsigned effect_clock;      /* 120 ms effect animation */
    unsigned reveal_clock;      /* 100 ms hole reveal */
    unsigned hole;              /* current mole hole index (native 0/1) */
    unsigned revealed;          /* holes revealed so far (13 total) */
    unsigned result;            /* 1 = success, 0 = failure */
    unsigned settled;
    unsigned difficulty;        /* 0..2, GetConfig AllMiniGame/Difficult */
    int last_x, last_y, have_last, last_sign_x, last_sign_y;
    double meter;               /* 0..100 */
    double hammer_t, hammer_h;  /* h = meter*t - 4.9*t*t */
    unsigned sound;             /* pending sound effect id, -1 = none */
} KHummer;
#endif

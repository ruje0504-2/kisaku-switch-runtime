#ifndef KISAKU_TENNIS_H
#define KISAKU_TENNIS_H
#include <stddef.h>
#include <stdint.h>
/* CTennis (main=31 / sub=610, 4fd950 special case `cmp sub,0x262` -> thunk
 * 0x4fdbfc -> handler 0x4fb8e0 = { CTennis t; if (t.Init() at 463170)
 * t.Exec(&flag) at 4609a0; }).
 *
 * Contract: the script pushes nothing (tennis.mes+0x24b is
 * `push 10; push 600; add; push 31; syscall`); the main loop
 * (vtable+0x68 = 0x463ca0) hands the result back through
 * vtable+0x7c = 0x46e870 -> 0x46f390 -> 0x402720, pushing the KValue at
 * CTennis+8.  The result is only 0 or 1 (0x463ed0 clears it, 0x463f30 sets
 * it).  tennis.mes stores it in byte[394] and does not branch; the caller
 * sun03_1.mes+0x5dd7 reads 394: 1 -> victory branch, 0 -> the other branch.
 *
 * Rules (native): best of five games (first to 3 games), each game first to
 * 3 points with 3-3 going to a two point margin (the display is clamped by
 * min(x,3)), the server side flips every game ([+0x104]).  A point runs
 * 0x462dd0 -> 0x4623a0 -> 0x4613b0/0x461ba0 (mirrored teams) -> per-frame
 * ball advance 0x460ba0 (return codes 0/1/2..7 index the landing table
 * 0x548328; 8 = user hit, 9 = cpu hit).  The difficulty comes from
 * GetConfig("AllMiniGame","Difficult") clamped to 0..2 (0x4647c0) and selects
 * one of three trajectories, 30.0/32.0/40.0 with angles -2.90/-3.00/-3.75
 * degrees.  The engine reads no keyboard or mouse input at all: both players
 * are driven by the program, so the original match is a spectator event.
 *
 * The port reproduces the rule set and the result contract.  The per-point
 * rally is resolved from the shared MSVC LCG instead of the native 3D ball
 * physics (the trajectory integration is not reproducible here); the player
 * win probability is reduced by the configured difficulty.  This deviation is
 * recorded in reports/porting.md. */
#define KTENNIS_GAMES_TO_WIN 3
#define KTENNIS_POINTS_TO_WIN 3
#define KTENNIS_POINT_MS 600
#define KTENNIS_FASTFORWARD_MS 75          /* 8x while the skip key is held */

typedef struct KTennis {
    KImage player1, court1, court2, player2, pieces;
    unsigned active, settled, result;
    unsigned games_player, games_cpu;
    unsigned points_player, points_cpu;
    unsigned server;                   /* flips every game (native +0x104) */
    unsigned difficulty;
    unsigned clock, fast, points_played;
    unsigned sound;
} KTennis;
#endif

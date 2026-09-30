#ifndef KISAKU_BINGO_H
#define KISAKU_BINGO_H
#include <stddef.h>
#include <stdint.h>
/* CBingo (main=31 / sub=711 neighbour: sub=611, thunk 0x4fdc09 -> 0x4fb7f0).
 *
 * Native contract: the script pushes nothing (bingo.mes+0x275 is
 * `push 10; push 601; add; push 31; syscall`) and the handler itself pops
 * nothing.  Only the 0x4d7ca0 failure path pushes an integer 0; the success
 * path pushes nothing at all, so bingo.mes+0x276 (`push 512; storebyte`)
 * reads a stack leftover in the original.  The port implements the playable
 * reading of that interface (documented deviation): one line completed ->
 * push 1, otherwise 0, which is what the callers actually branch on
 * (sun04_1.mes+0xB02B reward path, sat02_2..sat08_2 unlocks).
 *
 * Engine facts (0x4d73f0 pool, 0x4d74a0 deal, 0x4d6f10 line check):
 *   32 balls, Fisher-Yates `for i in 0..30: j = i + rand()%(32-i); swap`
 *   5x5 card, centre (2,2) free, 24 dealt numbers
 *   12 lines: 5 rows + 5 columns + both diagonals
 *   card cell (col,row) -> x = col*64+44, y = row*64+52, 32x24 from the
 *   20x9 digit atlas (source (32*(n%20), 24*(n/20))), one ball per 1000 ms
 *   with two trailing 1000 ms beats, Shift/Ctrl fast-forwards at 4x. */
#define KBINGO_BALLS 32
#define KBINGO_SIDE 5
#define KBINGO_WAIT_MS 1000
#define KBINGO_FASTFORWARD_MS 250          /* 4x, native [vt+0xa8] 0x102/0x103 */

typedef struct KBingo {
    KImage background, logo, parts, animation;
    unsigned active, settled, result;
    unsigned pool[KBINGO_BALLS];
    unsigned card[KBINGO_SIDE * KBINGO_SIDE];
    unsigned hit[KBINGO_SIDE * KBINGO_SIDE];
    unsigned drawn;                        /* balls consumed so far */
    unsigned lines;                        /* completed line mask, 12 bits */
    unsigned clock, tail, fast;            /* ms accounting */
    unsigned sound;
} KBingo;

static inline unsigned kbingo_rand(uint32_t *state, unsigned bound) {
    *state = *state * 214013u + 2531011u;   /* same MSVC LCG as VM opcode 0x39 */
    return ((*state >> 16) & 32767u) % bound;
}

/* 0x4d73f0 pool + 0x4d74a0 deal: shuffle 32 balls and lay the first 24 into
   the card, leaving the centre cell free (hit = 1). */
static inline void kbingo_deal(KBingo *k, uint32_t *rng) {
    for (unsigned i = 0; i < KBINGO_BALLS; i++) k->pool[i] = i;
    for (unsigned i = 0; i < KBINGO_BALLS - 1; i++) {
        unsigned j = i + kbingo_rand(rng, KBINGO_BALLS - i);
        unsigned tmp = k->pool[i]; k->pool[i] = k->pool[j]; k->pool[j] = tmp;
    }
    unsigned next = 0;
    for (unsigned cell = 0; cell < KBINGO_SIDE * KBINGO_SIDE; cell++) {
        unsigned row = cell / KBINGO_SIDE, col = cell % KBINGO_SIDE;
        if (row == 2 && col == 2) { k->card[cell] = 0; k->hit[cell] = 1; continue; }
        k->card[cell] = k->pool[next++];
        k->hit[cell] = 0;
    }
}

/* 0x4d6f10: five rows, five columns and both diagonals. */
static inline unsigned kbingo_lines(const KBingo *k) {
    unsigned mask = 0, bit = 0;
    for (unsigned row = 0; row < KBINGO_SIDE; row++, bit++) {
        unsigned all = 1;
        for (unsigned col = 0; col < KBINGO_SIDE; col++) all &= k->hit[row * KBINGO_SIDE + col];
        if (all) mask |= 1u << bit;
    }
    for (unsigned col = 0; col < KBINGO_SIDE; col++, bit++) {
        unsigned all = 1;
        for (unsigned row = 0; row < KBINGO_SIDE; row++) all &= k->hit[row * KBINGO_SIDE + col];
        if (all) mask |= 1u << bit;
    }
    unsigned all = 1;
    for (unsigned i = 0; i < KBINGO_SIDE; i++) all &= k->hit[i * KBINGO_SIDE + i];
    if (all) mask |= 1u << bit++;
    all = 1;
    for (unsigned i = 0; i < KBINGO_SIDE; i++) all &= k->hit[i * KBINGO_SIDE + (KBINGO_SIDE - 1 - i)];
    if (all) mask |= 1u << bit;
    return mask;
}

/* One drawn ball: mark the matching card cell (if any) and re-check lines. */
static inline void kbingo_draw(KBingo *k, unsigned ball) {
    for (unsigned cell = 0; cell < KBINGO_SIDE * KBINGO_SIDE; cell++) {
        unsigned row = cell / KBINGO_SIDE, col = cell % KBINGO_SIDE;
        if (row == 2 && col == 2) continue;
        if (k->card[cell] == ball) { k->hit[cell] = 1; break; }
    }
    k->lines = kbingo_lines(k);
}
#endif

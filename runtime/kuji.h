#ifndef KISAKU_KUJI_H
#define KISAKU_KUJI_H
#include <stddef.h>
#include <stdint.h>
/* CKuji (main=31 / sub=710), native handler 0x4fb400 -> 0x48fcf0.
 *
 * The script (kuji.mes+0x2ce) pushes four prize ids taken from
 * byte[1511..1514] and the engine appends the hard-coded blank id 9
 * (0x48fe8c), shuffles the five ids into the five bottom slots with
 * rand()%5 plus linear probing (0x435775 is the same MSVC LCG the VM runs
 * for opcode 0x39: state = state*214013 + 2531011, output (state>>16)&32767)
 * and finally pushes the prize reached from the selected column through the
 * fixed permutation table 0x546f98 = {2,4,0,3,1}.  kuji.mes stores the
 * pushed value in byte[1515]; byte 9 means blank (and is also what the
 * failure path writes, so the script cannot tell them apart).
 *
 * Layout constants (all verified from the native initialisers):
 *   column centres x = 78/215/321/445/569
 *   bottom slots   = 80x80 at (cx-40, 600)
 *   hit rectangles = (cx-60, cy-98, cx+60, cy+48) around the path end
 *   highlight cursor = column centre - (54, 92)
 * The logical canvas is at least 640x736 while the display surface is
 * 640x480, so the native must scroll or page; that part is still unresolved
 * (see deepseek/analysis/minigame-kuji.md section 8.2). */
#define KKUJI_COLUMNS 5
#define KKUJI_BLANK 9
#define KKUJI_STEP_MS 20          /* 0x4e2ee0(1,20) */
#define KKUJI_TAIL_MS 1000        /* trailing wait after the walk */
static const unsigned kkuji_perm[KKUJI_COLUMNS] = {2, 4, 0, 3, 1};
static const int kkuji_column_x[KKUJI_COLUMNS] = {78, 215, 321, 445, 569};

typedef struct KKuji {
    KImage background, prompt, cursor;
    KImage pages[KKUJI_COLUMNS];
    unsigned pool[KKUJI_COLUMNS];   /* four script ids plus the blank 9 */
    unsigned slot[KKUJI_COLUMNS];   /* shuffled bottom slots (native obj+0xac) */
    unsigned select;                /* highlighted column, 0..4 */
    unsigned result;                /* prize pushed back to the script */
    unsigned active;                /* modal owns the VM while set */
    unsigned chosen;                /* confirmation accepted */
    unsigned clock;                 /* milliseconds since the walk started */
    unsigned drawn;                 /* result picture requested for this column */
    unsigned viewport_y;            /* 640x480 window into the native 640x736 canvas */
} KKuji;

/* 415710 / 0x435775: shared MSVC LCG, identical to VM opcode 0x39. */
static inline unsigned kkuji_rand(uint32_t *state, unsigned bound) {
    *state = *state * 214013u + 2531011u;
    return ((*state >> 16) & 32767u) % bound;
}

/* 0x48fcf0: the four script ids plus the hard-coded blank, uniformly placed
   into the five bottom slots with rand()%5 and linear probing. */
static inline void kkuji_shuffle(KKuji *k, uint32_t *rng) {
    unsigned pending[KKUJI_COLUMNS];
    unsigned used[KKUJI_COLUMNS] = {0, 0, 0, 0, 0};
    for (unsigned i = 0; i < KKUJI_COLUMNS; i++) k->slot[i] = KKUJI_BLANK;
    for (unsigned i = 0; i < 4; i++) pending[i] = k->pool[i];
    pending[4] = KKUJI_BLANK;
    for (unsigned i = 0; i < KKUJI_COLUMNS; i++) {
        unsigned slot = kkuji_rand(rng, KKUJI_COLUMNS);
        while (used[slot]) slot = (slot + 1) % KKUJI_COLUMNS;
        used[slot] = 1;
        k->slot[slot] = pending[i];
    }
}
#endif

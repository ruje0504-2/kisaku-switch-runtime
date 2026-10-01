#ifndef KISAKU_STAFFROLL_H
#define KISAKU_STAFFROLL_H
#include <stddef.h>
#include "rmt.h"
#include "ax.h"
/* CStaff end screen (main=31 / sub=210, handler 0x4fb9e0).  One integer
   operand = mode (1 = full end roll, 0 = short end roll); no VM write-back,
   only bytes[4012] = 1 on entry and 0 on exit.  Both native rolls have
   twelve AX resources: the sixth is the special p06s/p6ms transition. */
typedef struct KStaffroll {
    KImage background, part1, part2, part3, staff1, staff2;
    struct ax_player ax;
    uint8_t ax_events[AX_CELLS];
    unsigned active;
    unsigned mode;        /* 1 = full, 0 = short */
    unsigned ticks;       /* 2100 full / 1050 short at 20 ms per tick */
    unsigned segment_count; /* 12 resources, including the special sixth */
    unsigned elapsed;
    unsigned clock;
    int scroll;            /* [this+0xdc] += 4 with a -640 wrap */
    unsigned subtitle_page;/* 44-page schedule; 0..26 uses staff1 */
    unsigned prepared;    /* current base frame has been rebuilt before AX draws */
    unsigned segment;
    unsigned segment_clock;
    unsigned source_part; /* AX source: 1=endpart1(.m), 2=endpart2(.m) */
} KStaffroll;
#endif

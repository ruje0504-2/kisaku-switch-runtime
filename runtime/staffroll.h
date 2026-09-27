#ifndef KISAKU_STAFFROLL_H
#define KISAKU_STAFFROLL_H
#include <stddef.h>
/* CStaff end screen (main=31 / sub=210, handler 0x4fb9e0).  One integer
   operand = mode (1 = full end_p01..p11.ax roll, 0 = short end_p1m..p11m);
   no VM write-back, only bytes[4012] = 1 on entry and 0 on exit. */
typedef struct KStaffroll {
    unsigned active;
    unsigned mode;        /* 1 = full, 0 = short */
    unsigned ticks;       /* 2100 full / 1050 short at 20 ms per tick */
    unsigned elapsed;
    unsigned clock;
    unsigned scroll;      /* [this+0xdc] += 4 with a -640 wrap */
} KStaffroll;
#endif
